/*
 * mediaplayer.c
 *      MediaPlayer layer wrapping FFPlayer with state machine and message queue
 *      Based on ijkplayer's ijkplayer.c
 *
 * Copyright (c) 2013 Bilibili
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "mediaplayer.h"
#include "ff_ffplayer.h"

#ifdef _WIN32
#include <windows.h>
#define pthread_mutex_t         CRITICAL_SECTION
#define pthread_mutex_init(m,a) InitializeCriticalSection(m)
#define pthread_mutex_destroy   DeleteCriticalSection
#define pthread_mutex_lock      EnterCriticalSection
#define pthread_mutex_unlock    LeaveCriticalSection
#else
#include <pthread.h>
#endif

/*
 * =============================================================================
 * MediaPlayer 结构体定义
 * =============================================================================
 */
struct MediaPlayer {
    volatile int ref_count;         /* 引用计数 */
    pthread_mutex_t mutex;          /* 互斥锁 */
    
    FFPlayer *ffplayer;             /* FFPlayer 实例 */
    MessageQueue msg_queue;         /* 消息队列 */
    
    int mp_state;                   /* 播放器状态 */
    char *data_source;              /* 数据源 URL */
    void *weak_thiz;                /* 用户数据指针 */
    
    int restart;                    /* 重启标志 */
    int restart_from_beginning;     /* 从头重启标志 */
    int seek_req;                   /* seek 请求标志 */
    long seek_msec;                 /* seek 目标位置 */
};

/*
 * =============================================================================
 * 辅助宏定义
 * =============================================================================
 */

/* 状态检查宏 - 如果状态匹配则返回错误 */
#define MPST_RET_IF_EQ_INT(real, expected, errcode) \
    do { \
        if ((real) == (expected)) return (errcode); \
    } while(0)

#define MPST_RET_IF_EQ(real, expected) \
    MPST_RET_IF_EQ_INT(real, expected, MP_ERR_INVALID_STATE)

/* 返回值检查宏 */
#define MP_RET_IF_FAILED(ret) \
    do { \
        int retval = ret; \
        if (retval != 0) return (retval); \
    } while(0)

/*
 * =============================================================================
 * 内部函数声明
 * =============================================================================
 */
static void mp_change_state_l(MediaPlayer *mp, int new_state);
static void mp_destroy(MediaPlayer *mp);

/* 带锁的内部函数 */
static int mp_set_data_source_l(MediaPlayer *mp, const char *url);
static int mp_prepare_async_l(MediaPlayer *mp);
static int mp_start_l(MediaPlayer *mp);
static int mp_pause_l(MediaPlayer *mp);
static int mp_stop_l(MediaPlayer *mp);
static int mp_seek_to_l(MediaPlayer *mp, long msec);
static long mp_get_current_position_l(MediaPlayer *mp);
static long mp_get_duration_l(MediaPlayer *mp);
static long mp_get_playable_duration_l(MediaPlayer *mp);

/* 状态检查函数 */
static int mp_chkst_start_l(int mp_state);
static int mp_chkst_pause_l(int mp_state);
static int mp_chkst_seek_l(int mp_state);

/*
 * =============================================================================
 * FFPlayer 消息通知函数（供 FFPlayer 调用）
 * =============================================================================
 */
void ffp_notify_msg1(FFPlayer *ffp, int what);
void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1);
void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2);

/*
 * =============================================================================
 * 全局初始化/反初始化
 * =============================================================================
 */

void mp_global_init(void)
{
    ffp_global_init();
}

void mp_global_uninit(void)
{
    ffp_global_uninit();
}

/*
 * =============================================================================
 * 状态变更
 * =============================================================================
 */

static void mp_change_state_l(MediaPlayer *mp, int new_state)
{
    mp->mp_state = new_state;
    /* 发送状态变化消息 */
    msg_queue_put_simple1(&mp->msg_queue, FFP_MSG_PLAYBACK_STATE_CHANGED);
}

/* mp_msg_loop 已移除，消息循环由上层（Qt/Java）驱动，调用 mp_get_msg */

/*
 * =============================================================================
 * 状态检查函数
 * =============================================================================
 */

static int mp_chkst_start_l(int mp_state)
{
    MPST_RET_IF_EQ(mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ASYNC_PREPARING);
    /* MP_STATE_PREPARED - OK */
    /* MP_STATE_STARTED - OK */
    /* MP_STATE_PAUSED - OK */
    /* MP_STATE_COMPLETED - OK */
    MPST_RET_IF_EQ(mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp_state, MP_STATE_END);
    
    return 0;
}

static int mp_chkst_pause_l(int mp_state)
{
    MPST_RET_IF_EQ(mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ASYNC_PREPARING);
    /* MP_STATE_PREPARED - OK */
    /* MP_STATE_STARTED - OK */
    /* MP_STATE_PAUSED - OK */
    /* MP_STATE_COMPLETED - OK */
    MPST_RET_IF_EQ(mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp_state, MP_STATE_END);
    
    return 0;
}

static int mp_chkst_seek_l(int mp_state)
{
    MPST_RET_IF_EQ(mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ASYNC_PREPARING);
    /* MP_STATE_PREPARED - OK */
    /* MP_STATE_STARTED - OK */
    /* MP_STATE_PAUSED - OK */
    /* MP_STATE_COMPLETED - OK */
    MPST_RET_IF_EQ(mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp_state, MP_STATE_END);
    
    return 0;
}

/*
 * =============================================================================
 * MediaPlayer 生命周期管理
 * =============================================================================
 */

MediaPlayer *mp_create(void)
{
    MediaPlayer *mp = (MediaPlayer *)calloc(1, sizeof(MediaPlayer));
    if (!mp) {
        av_log(NULL, AV_LOG_ERROR, "[MediaPlayer] Failed to allocate MediaPlayer\n");
        return NULL;
    }
    
    /* 初始化互斥锁 */
    pthread_mutex_init(&mp->mutex, NULL);
    
    /* 创建 FFPlayer */
    mp->ffplayer = ffp_create();
    if (!mp->ffplayer) {
        av_log(NULL, AV_LOG_ERROR, "[MediaPlayer] Failed to create FFPlayer\n");
        pthread_mutex_destroy(&mp->mutex);
        free(mp);
        return NULL;
    }
    
    /* 初始化消息队列 */
    msg_queue_init(&mp->msg_queue);
    
    /* 建立 FFPlayer 到 MediaPlayer 消息队列的关联 */
    mp->ffplayer->ext_msg_queue = &mp->msg_queue;
    
    /* 初始化状态 */
    mp->mp_state = MP_STATE_IDLE;
    mp->ref_count = 1;
    mp->data_source = NULL;
    mp->weak_thiz = NULL;
    mp->restart = 0;
    mp->restart_from_beginning = 0;
    mp->seek_req = 0;
    mp->seek_msec = 0;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] Created MediaPlayer %p\n", mp);
    return mp;
}

static void mp_destroy(MediaPlayer *mp)
{
    if (!mp)
        return;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] Destroying MediaPlayer %p\n", mp);
    
    /* 确保 shutdown 已经被调用 */
    mp_shutdown(mp);
    
    /* 销毁 FFPlayer（完全清理，包括窗口和 SDL）*/
    if (mp->ffplayer) {
        ffp_shutdown(mp->ffplayer);
        mp->ffplayer = NULL;
    }
    
    /* 销毁消息队列 */
    msg_queue_destroy(&mp->msg_queue);
    
    /* 释放数据源 */
    if (mp->data_source) {
        free(mp->data_source);
        mp->data_source = NULL;
    }
    
    /* 销毁互斥锁 */
    pthread_mutex_destroy(&mp->mutex);
    
    /* 释放结构体 */
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] MediaPlayer destroyed\n");
    memset(mp, 0, sizeof(MediaPlayer));
    free(mp);
}

void mp_release(MediaPlayer *mp)
{
    mp_dec_ref(mp);
}

void mp_inc_ref(MediaPlayer *mp)
{
    if (!mp)
        return;
    
#ifdef _WIN32
    InterlockedIncrement((volatile LONG *)&mp->ref_count);
#else
    __sync_fetch_and_add(&mp->ref_count, 1);
#endif
}

void mp_dec_ref(MediaPlayer *mp)
{
    if (!mp)
        return;
    
    int ref_count;
#ifdef _WIN32
    ref_count = InterlockedDecrement((volatile LONG *)&mp->ref_count);
#else
    ref_count = __sync_sub_and_fetch(&mp->ref_count, 1);
#endif
    
    if (ref_count == 0) {
        av_log(NULL, AV_LOG_INFO, "[MediaPlayer] ref_count=0, destroying\n");
        mp_shutdown(mp);
        mp_destroy(mp);
    }
}

void mp_dec_ref_p(MediaPlayer **pmp)
{
    if (!pmp)
        return;
    
    mp_dec_ref(*pmp);
    *pmp = NULL;
}

void mp_reset(MediaPlayer *mp)
{
    if (!mp)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    
    /* 停止播放 */
    if (mp->ffplayer) {
        ffp_stop(mp->ffplayer);
    }
    
    /* 重置状态 */
    mp->mp_state = MP_STATE_IDLE;
    mp->restart = 0;
    mp->restart_from_beginning = 0;
    mp->seek_req = 0;
    mp->seek_msec = 0;
    
    /* 释放数据源 */
    if (mp->data_source) {
        free(mp->data_source);
        mp->data_source = NULL;
    }
    
    pthread_mutex_unlock(&mp->mutex);
}

void mp_shutdown(MediaPlayer *mp)
{
    if (!mp)
        return;
    
    /* 防止重复 shutdown */
    if (mp->mp_state == MP_STATE_END) {
        av_log(NULL, AV_LOG_DEBUG, "[MediaPlayer] Already shutdown, skipping\n");
        return;
    }
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] Shutting down\n");
    
    /* 中止消息队列（唤醒上层的消息循环线程） */
    msg_queue_abort(&mp->msg_queue);
    
    /* 停止 FFPlayer (包括渲染线程) */
    if (mp->ffplayer) {
        /* 先停止渲染线程 */
        ffp_stop_render_thread(mp->ffplayer);
        /* 再停止播放流 */
        ffp_stop(mp->ffplayer);
    }
    
    mp->mp_state = MP_STATE_END;
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] Shutdown complete\n");
}

/*
 * =============================================================================
 * 窗口/渲染管理
 * =============================================================================
 */

int mp_attach_window(MediaPlayer *mp, void *window_handle, int width, int height)
{
    if (!mp || !mp->ffplayer)
        return MP_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&mp->mutex);
    int ret = ffp_attach_window(mp->ffplayer, window_handle, width, height);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

/*
 * =============================================================================
 * 播放控制 - 内部函数（需要外部加锁）
 * =============================================================================
 */

static int mp_set_data_source_l(MediaPlayer *mp, const char *url)
{
    assert(mp);
    assert(url);
    
    /* 状态检查：只有 IDLE 状态可以设置数据源 */
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_INITIALIZED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ASYNC_PREPARING);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PREPARED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STARTED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PAUSED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_COMPLETED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STOPPED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_END);
    
    /* 释放旧的数据源 */
    if (mp->data_source) {
        free(mp->data_source);
        mp->data_source = NULL;
    }
    
    /* 复制新的数据源 */
    mp->data_source = strdup(url);
    if (!mp->data_source)
        return MP_ERR_OUT_OF_MEMORY;
    
    mp_change_state_l(mp, MP_STATE_INITIALIZED);
    return 0;
}

static int mp_prepare_async_l(MediaPlayer *mp)
{
    assert(mp);
    
    /* 状态检查 */
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_IDLE);
    /* MP_STATE_INITIALIZED - OK */
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ASYNC_PREPARING);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PREPARED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STARTED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PAUSED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_COMPLETED);
    /* MP_STATE_STOPPED - OK (可以重新 prepare) */
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_END);
    
    if (!mp->data_source)
        return MP_ERR_INVALID_PARAM;
    
    mp_change_state_l(mp, MP_STATE_ASYNC_PREPARING);
    
    /* 启动消息队列 */
    msg_queue_start(&mp->msg_queue);
    
    /* 
     * 注意：消息循环由上层（Qt/Java）驱动，不在这里启动线程
     * 上层需要调用 mp_get_msg() 来驱动消息处理
     */
    
    /* 调用 FFPlayer 准备 */
    int ret = ffp_prepare_async(mp->ffplayer, mp->data_source);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "[MediaPlayer] ffp_prepare_async failed: %d\n", ret);
        msg_queue_put_simple2(&mp->msg_queue, FFP_MSG_ERROR, ret);
        mp_change_state_l(mp, MP_STATE_ERROR);
        return ret;
    }
    
    /* 
     * 注意：不在这里发送 FFP_MSG_PREPARED！
     * FFP_MSG_PREPARED 应该由 FFPlayer 的 read_thread/stream_open 在真正准备完成后发送
     * 通过 ffp_notify_msg1(ffp, FFP_MSG_PREPARED) 发送到 ext_msg_queue
     */
    
    return 0;
}

static int mp_start_l(MediaPlayer *mp)
{
    assert(mp);
    
    MP_RET_IF_FAILED(mp_chkst_start_l(mp->mp_state));
    
    /* 移除冲突的消息 */
    msg_queue_remove(&mp->msg_queue, FFP_REQ_START);
    msg_queue_remove(&mp->msg_queue, FFP_REQ_PAUSE);
    
    /* 发送开始请求 */
    msg_queue_put_simple1(&mp->msg_queue, FFP_REQ_START);
    
    return 0;
}

static int mp_pause_l(MediaPlayer *mp)
{
    assert(mp);
    
    MP_RET_IF_FAILED(mp_chkst_pause_l(mp->mp_state));
    
    /* 移除冲突的消息 */
    msg_queue_remove(&mp->msg_queue, FFP_REQ_START);
    msg_queue_remove(&mp->msg_queue, FFP_REQ_PAUSE);
    
    /* 发送暂停请求 */
    msg_queue_put_simple1(&mp->msg_queue, FFP_REQ_PAUSE);
    
    return 0;
}

static int mp_stop_l(MediaPlayer *mp)
{
    assert(mp);
    
    /* 状态检查 */
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_INITIALIZED);
    /* MP_STATE_ASYNC_PREPARING - OK */
    /* MP_STATE_PREPARED - OK */
    /* MP_STATE_STARTED - OK */
    /* MP_STATE_PAUSED - OK */
    /* MP_STATE_COMPLETED - OK */
    /* MP_STATE_STOPPED - OK */
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_END);
    
    /* 移除所有请求消息 */
    msg_queue_remove(&mp->msg_queue, FFP_REQ_START);
    msg_queue_remove(&mp->msg_queue, FFP_REQ_PAUSE);
    msg_queue_remove(&mp->msg_queue, FFP_REQ_SEEK);
    
    /* 调用 FFPlayer 停止 */
    if (mp->ffplayer) {
        ffp_stop(mp->ffplayer);
    }
    
    mp_change_state_l(mp, MP_STATE_STOPPED);
    return 0;
}

static int mp_seek_to_l(MediaPlayer *mp, long msec)
{
    assert(mp);
    
    MP_RET_IF_FAILED(mp_chkst_seek_l(mp->mp_state));
    
    mp->seek_req = 1;
    mp->seek_msec = msec;
    
    /* 移除旧的 seek 请求 */
    msg_queue_remove(&mp->msg_queue, FFP_REQ_SEEK);
    
    /* 发送 seek 请求 */
    msg_queue_put_simple2(&mp->msg_queue, FFP_REQ_SEEK, (int)msec);
    
    return 0;
}

static long mp_get_current_position_l(MediaPlayer *mp)
{
    if (mp->seek_req)
        return mp->seek_msec;
    
    if (mp->ffplayer)
        return ffp_get_current_position(mp->ffplayer);
    
    return 0;
}

static long mp_get_duration_l(MediaPlayer *mp)
{
    if (mp->ffplayer)
        return ffp_get_duration(mp->ffplayer);
    
    return 0;
}

static long mp_get_playable_duration_l(MediaPlayer *mp)
{
    if (mp->ffplayer)
        return ffp_get_playable_duration(mp->ffplayer);
    
    return 0;
}

/*
 * =============================================================================
 * 播放控制 - 公开函数
 * =============================================================================
 */

int mp_set_data_source(MediaPlayer *mp, const char *url)
{
    if (!mp || !url)
        return MP_ERR_INVALID_PARAM;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] mp_set_data_source: %s\n", url);
    
    pthread_mutex_lock(&mp->mutex);
    int ret = mp_set_data_source_l(mp, url);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

int mp_prepare_async(MediaPlayer *mp)
{
    if (!mp)
        return MP_ERR_INVALID_PARAM;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] mp_prepare_async\n");
    
    pthread_mutex_lock(&mp->mutex);
    int ret = mp_prepare_async_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

int mp_start(MediaPlayer *mp)
{
    if (!mp)
        return MP_ERR_INVALID_PARAM;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] mp_start\n");
    
    pthread_mutex_lock(&mp->mutex);
    int ret = mp_start_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

int mp_pause(MediaPlayer *mp)
{
    if (!mp)
        return MP_ERR_INVALID_PARAM;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] mp_pause\n");
    
    pthread_mutex_lock(&mp->mutex);
    int ret = mp_pause_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

int mp_stop(MediaPlayer *mp)
{
    if (!mp)
        return MP_ERR_INVALID_PARAM;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] mp_stop\n");
    
    pthread_mutex_lock(&mp->mutex);
    int ret = mp_stop_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

int mp_seek_to(MediaPlayer *mp, long msec)
{
    if (!mp)
        return MP_ERR_INVALID_PARAM;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] mp_seek_to: %ld ms\n", msec);
    
    pthread_mutex_lock(&mp->mutex);
    int ret = mp_seek_to_l(mp, msec);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

void mp_seek_relative(MediaPlayer *mp, double incr)
{
    if (!mp || !mp->ffplayer)
        return;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] mp_seek_relative: %.2f sec\n", incr);
    
    pthread_mutex_lock(&mp->mutex);
    ffp_seek_relative(mp->ffplayer, incr);
    pthread_mutex_unlock(&mp->mutex);
}

/*
 * =============================================================================
 * 状态查询
 * =============================================================================
 */

int mp_get_state(MediaPlayer *mp)
{
    if (!mp)
        return MP_STATE_END;
    
    return mp->mp_state;
}

bool mp_is_playing(MediaPlayer *mp)
{
    if (!mp)
        return false;
    
    return (mp->mp_state == MP_STATE_PREPARED ||
            mp->mp_state == MP_STATE_STARTED);
}

long mp_get_current_position(MediaPlayer *mp)
{
    if (!mp)
        return 0;
    
    pthread_mutex_lock(&mp->mutex);
    long ret = mp_get_current_position_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

long mp_get_duration(MediaPlayer *mp)
{
    if (!mp)
        return 0;
    
    pthread_mutex_lock(&mp->mutex);
    long ret = mp_get_duration_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

long mp_get_playable_duration(MediaPlayer *mp)
{
    if (!mp)
        return 0;
    
    pthread_mutex_lock(&mp->mutex);
    long ret = mp_get_playable_duration_l(mp);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

/*
 * =============================================================================
 * 音量控制
 * =============================================================================
 */

void mp_set_volume(MediaPlayer *mp, float volume)
{
    if (!mp || !mp->ffplayer)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_set_volume(mp->ffplayer, volume);
    pthread_mutex_unlock(&mp->mutex);
}

float mp_get_volume(MediaPlayer *mp)
{
    if (!mp || !mp->ffplayer)
        return 0.0f;
    
    pthread_mutex_lock(&mp->mutex);
    float ret = ffp_get_volume(mp->ffplayer);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

void mp_update_volume(MediaPlayer *mp, int sign, double step)
{
    if (!mp || !mp->ffplayer)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_update_volume(mp->ffplayer, sign, step);
    pthread_mutex_unlock(&mp->mutex);
}

void mp_toggle_mute(MediaPlayer *mp)
{
    if (!mp || !mp->ffplayer)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_toggle_mute(mp->ffplayer);
    pthread_mutex_unlock(&mp->mutex);
}

void mp_set_mute(MediaPlayer *mp, int mute)
{
    if (!mp || !mp->ffplayer)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_set_mute(mp->ffplayer, mute);
    pthread_mutex_unlock(&mp->mutex);
}

int mp_is_muted(MediaPlayer *mp)
{
    if (!mp || !mp->ffplayer)
        return 0;
    
    pthread_mutex_lock(&mp->mutex);
    int ret = ffp_is_muted(mp->ffplayer);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

/*
 * =============================================================================
 * 逐帧播放控制
 * =============================================================================
 */

void mp_step_to_next_frame(MediaPlayer *mp)
{
    if (!mp || !mp->ffplayer)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_step_to_next_frame(mp->ffplayer);
    pthread_mutex_unlock(&mp->mutex);
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] Step to next frame\n");
}

/*
 * =============================================================================
 * 播放速率控制
 * =============================================================================
 */

int mp_set_playback_rate(MediaPlayer *mp, float rate)
{
    if (!mp || !mp->ffplayer)
        return -1;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_set_playback_rate(mp->ffplayer, rate);
    pthread_mutex_unlock(&mp->mutex);
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] Playback rate set to: %.2f\n", rate);
    return 0;
}

float mp_get_playback_rate(MediaPlayer *mp)
{
    if (!mp || !mp->ffplayer)
        return 1.0f;
    
    pthread_mutex_lock(&mp->mutex);
    float rate = mp->ffplayer->playback_rate;
    pthread_mutex_unlock(&mp->mutex);
    
    return rate;
}

/*
 * =============================================================================
 * 循环控制
 * =============================================================================
 */

void mp_set_loop(MediaPlayer *mp, int loop)
{
    if (!mp || !mp->ffplayer)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_set_loop(mp->ffplayer, loop);
    pthread_mutex_unlock(&mp->mutex);
}

int mp_get_loop(MediaPlayer *mp)
{
    if (!mp || !mp->ffplayer)
        return 0;
    
    pthread_mutex_lock(&mp->mutex);
    int ret = ffp_get_loop(mp->ffplayer);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

/*
 * =============================================================================
 * 选项设置
 * =============================================================================
 */

void mp_set_option(MediaPlayer *mp, int opt_category, const char *name, const char *value)
{
    if (!mp || !mp->ffplayer || !name)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_set_option(mp->ffplayer, opt_category, name, value);
    pthread_mutex_unlock(&mp->mutex);
}

void mp_set_option_int(MediaPlayer *mp, int opt_category, const char *name, int64_t value)
{
    if (!mp || !mp->ffplayer || !name)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_set_option_int(mp->ffplayer, opt_category, name, value);
    pthread_mutex_unlock(&mp->mutex);
}

/*
 * =============================================================================
 * 属性访问
 * =============================================================================
 */

float mp_get_property_float(MediaPlayer *mp, int id, float default_value)
{
    if (!mp || !mp->ffplayer)
        return default_value;
    
    pthread_mutex_lock(&mp->mutex);
    float ret = ffp_get_property_float(mp->ffplayer, id, default_value);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

void mp_set_property_float(MediaPlayer *mp, int id, float value)
{
    if (!mp || !mp->ffplayer)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_set_property_float(mp->ffplayer, id, value);
    pthread_mutex_unlock(&mp->mutex);
}

int64_t mp_get_property_int64(MediaPlayer *mp, int id, int64_t default_value)
{
    if (!mp || !mp->ffplayer)
        return default_value;
    
    pthread_mutex_lock(&mp->mutex);
    int64_t ret = ffp_get_property_int64(mp->ffplayer, id, default_value);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

void mp_set_property_int64(MediaPlayer *mp, int id, int64_t value)
{
    if (!mp || !mp->ffplayer)
        return;
    
    pthread_mutex_lock(&mp->mutex);
    ffp_set_property_int64(mp->ffplayer, id, value);
    pthread_mutex_unlock(&mp->mutex);
}

/*
 * =============================================================================
 * 编解码器信息
 * =============================================================================
 */

int mp_get_video_codec_info(MediaPlayer *mp, char **codec_info)
{
    if (!mp || !mp->ffplayer || !codec_info)
        return MP_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&mp->mutex);
    int ret = ffp_get_video_codec_info(mp->ffplayer, codec_info);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

int mp_get_audio_codec_info(MediaPlayer *mp, char **codec_info)
{
    if (!mp || !mp->ffplayer || !codec_info)
        return MP_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&mp->mutex);
    int ret = ffp_get_audio_codec_info(mp->ffplayer, codec_info);
    pthread_mutex_unlock(&mp->mutex);
    
    return ret;
}

/*
 * =============================================================================
 * 消息队列
 * 
 * mp_get_msg 是 ijkplayer 风格的"过滤+处理"函数：
 * - 从队列取消息
 * - 处理所有消息（状态更新、执行 FFPlayer 操作）
 * - 过滤掉内部请求消息（FFP_REQ_*），继续取下一条
 * - 返回通知消息（FFP_MSG_*）给上层
 * 
 * 上层（Qt/Java/iOS）驱动消息循环，调用此函数
 * =============================================================================
 */

int mp_get_msg(MediaPlayer *mp, AVMessage *msg, int block)
{
    if (!mp || !msg)
        return -1;
    
    while (1) {
        int continue_wait_next_msg = 0;
        
        int retval = msg_queue_get(&mp->msg_queue, msg, block);
        if (retval <= 0)
            return retval;
        
        switch (msg->what) {
        case FFP_MSG_FLUSH:
            av_log(NULL, AV_LOG_DEBUG, "[mp_get_msg] FFP_MSG_FLUSH\n");
            break;
            
        case FFP_MSG_PREPARED:
            av_log(NULL, AV_LOG_INFO, "[mp_get_msg] FFP_MSG_PREPARED\n");
            pthread_mutex_lock(&mp->mutex);
            if (mp->mp_state == MP_STATE_ASYNC_PREPARING) {
                mp_change_state_l(mp, MP_STATE_PREPARED);
            } else {
                av_log(NULL, AV_LOG_WARNING, 
                       "[mp_get_msg] FFP_MSG_PREPARED: unexpected state %d\n", mp->mp_state);
            }
            pthread_mutex_unlock(&mp->mutex);
            break;
            
        case FFP_MSG_COMPLETED:
            av_log(NULL, AV_LOG_INFO, "[mp_get_msg] FFP_MSG_COMPLETED\n");
            pthread_mutex_lock(&mp->mutex);
            mp->restart = 1;
            mp->restart_from_beginning = 1;
            mp_change_state_l(mp, MP_STATE_COMPLETED);
            pthread_mutex_unlock(&mp->mutex);
            break;
            
        case FFP_MSG_ERROR:
            av_log(NULL, AV_LOG_ERROR, "[mp_get_msg] FFP_MSG_ERROR: %d\n", msg->arg1);
            pthread_mutex_lock(&mp->mutex);
            mp_change_state_l(mp, MP_STATE_ERROR);
            pthread_mutex_unlock(&mp->mutex);
            break;
            
        case FFP_MSG_SEEK_COMPLETE:
            av_log(NULL, AV_LOG_INFO, "[mp_get_msg] FFP_MSG_SEEK_COMPLETE\n");
            pthread_mutex_lock(&mp->mutex);
            mp->seek_req = 0;
            mp->seek_msec = 0;
            pthread_mutex_unlock(&mp->mutex);
            break;
            
        case FFP_REQ_START:
            av_log(NULL, AV_LOG_DEBUG, "[mp_get_msg] FFP_REQ_START\n");
            continue_wait_next_msg = 1;  /* 内部消息，不返回给上层 */
            pthread_mutex_lock(&mp->mutex);
            if (0 == mp_chkst_start_l(mp->mp_state)) {
                if (mp->restart) {
                    if (mp->restart_from_beginning) {
                        av_log(NULL, AV_LOG_DEBUG, "[mp_get_msg] restart from beginning\n");
                        if (mp->ffplayer) {
                            ffp_seek_to(mp->ffplayer, 0);
                            ffp_start(mp->ffplayer);
                        }
                        mp_change_state_l(mp, MP_STATE_STARTED);
                    } else {
                        av_log(NULL, AV_LOG_DEBUG, "[mp_get_msg] restart from current pos\n");
                        if (mp->ffplayer) {
                            ffp_start(mp->ffplayer);
                        }
                        mp_change_state_l(mp, MP_STATE_STARTED);
                    }
                    mp->restart = 0;
                    mp->restart_from_beginning = 0;
                } else {
                    av_log(NULL, AV_LOG_DEBUG, "[mp_get_msg] start on fly\n");
                    if (mp->ffplayer) {
                        ffp_start(mp->ffplayer);
                    }
                    mp_change_state_l(mp, MP_STATE_STARTED);
                }
            }
            pthread_mutex_unlock(&mp->mutex);
            break;
            
        case FFP_REQ_PAUSE:
            av_log(NULL, AV_LOG_DEBUG, "[mp_get_msg] FFP_REQ_PAUSE\n");
            continue_wait_next_msg = 1;  /* 内部消息，不返回给上层 */
            pthread_mutex_lock(&mp->mutex);
            if (0 == mp_chkst_pause_l(mp->mp_state)) {
                if (mp->ffplayer) {
                    ffp_pause(mp->ffplayer);
                }
                mp_change_state_l(mp, MP_STATE_PAUSED);
            }
            pthread_mutex_unlock(&mp->mutex);
            break;
            
        case FFP_REQ_SEEK:
            av_log(NULL, AV_LOG_DEBUG, "[mp_get_msg] FFP_REQ_SEEK: %d ms\n", msg->arg1);
            continue_wait_next_msg = 1;  /* 内部消息，不返回给上层 */
            pthread_mutex_lock(&mp->mutex);
            if (0 == mp_chkst_seek_l(mp->mp_state)) {
                mp->restart_from_beginning = 0;
                if (mp->ffplayer) {
                    ffp_seek_to(mp->ffplayer, msg->arg1);
                }
            }
            pthread_mutex_unlock(&mp->mutex);
            break;
            
        case FFP_MSG_VIDEO_SIZE_CHANGED:
            av_log(NULL, AV_LOG_INFO, "[mp_get_msg] FFP_MSG_VIDEO_SIZE_CHANGED: %dx%d\n", 
                   msg->arg1, msg->arg2);
            break;
            
        case FFP_MSG_BUFFERING_START:
        case FFP_MSG_BUFFERING_END:
        case FFP_MSG_PLAYBACK_STATE_CHANGED:
            /* 这些消息直接返回给上层 */
            break;
            
        default:
            av_log(NULL, AV_LOG_DEBUG, "[mp_get_msg] Unknown message: %d\n", msg->what);
            break;
        }
        
        /* 内部请求消息被消费掉，继续取下一条 */
        if (continue_wait_next_msg) {
            msg_free_res(msg);
            continue;
        }
        
        /* 通知消息返回给上层 */
        return retval;
    }
    
    return -1;
}

void mp_abort_msg_queue(MediaPlayer *mp)
{
    if (!mp)
        return;
    
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] Aborting message queue\n");
    msg_queue_abort(&mp->msg_queue);
}

/*
 * =============================================================================
 * 用户数据
 * =============================================================================
 */

void *mp_get_weak_thiz(MediaPlayer *mp)
{
    if (!mp)
        return NULL;
    
    return mp->weak_thiz;
}

void *mp_set_weak_thiz(MediaPlayer *mp, void *weak_thiz)
{
    if (!mp)
        return NULL;
    
    void *prev = mp->weak_thiz;
    mp->weak_thiz = weak_thiz;
    return prev;
}

/*
 * =============================================================================
 * 渲染模式设置
 * =============================================================================
 */

void mp_set_render_mode(MediaPlayer *mp, FFPRenderMode mode)
{
    if (!mp || !mp->ffplayer)
        return;
    
    ffp_set_render_mode(mp->ffplayer, mode);
}

FFPRenderMode mp_get_render_mode(MediaPlayer *mp)
{
    if (!mp || !mp->ffplayer)
        return FFP_RENDER_MODE_SDL;
    
    return ffp_get_render_mode(mp->ffplayer);
}

void mp_set_video_frame_callback(MediaPlayer *mp, ffp_video_frame_callback cb, void *opaque)
{
    if (!mp || !mp->ffplayer)
        return;
    
    ffp_set_video_frame_callback(mp->ffplayer, cb, opaque);
}

/*
 * =============================================================================
 * FFPlayer 访问
 * =============================================================================
 */

struct FFPlayer *mp_get_ffplayer(MediaPlayer *mp)
{
    if (!mp)
        return NULL;
    
    return mp->ffplayer;
}

/*
 * =============================================================================
 * FFPlayer 消息通知函数实现（供 FFPlayer 层调用）
 * 
 * 这些函数将消息从 FFPlayer 层发送到 MediaPlayer 层的消息队列
 * =============================================================================
 */

void ffp_notify_msg1(FFPlayer *ffp, int what)
{
    if (!ffp || !ffp->ext_msg_queue)
        return;
    msg_queue_put_simple1(ffp->ext_msg_queue, what);
}

void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1)
{
    if (!ffp || !ffp->ext_msg_queue)
        return;
    msg_queue_put_simple2(ffp->ext_msg_queue, what, arg1);
}

void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2)
{
    if (!ffp || !ffp->ext_msg_queue)
        return;
    msg_queue_put_simple3(ffp->ext_msg_queue, what, arg1, arg2);
}

