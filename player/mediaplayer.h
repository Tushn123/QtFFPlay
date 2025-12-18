/*
 * mediaplayer.h
 *      MediaPlayer layer wrapping FFPlayer with state machine and message queue
 *      Based on ijkplayer's ijkplayer.h
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

#ifndef MEDIAPLAYER_H
#define MEDIAPLAYER_H

#include "ff_ffmsg_queue.h"
#include "ff_ffplayer.h"  /* For FFPRenderMode, FFPVideoFrame, ffp_video_frame_callback */

#ifdef __cplusplus
extern "C" {
#endif

/* MSVC C 模式兼容性处理 */
#if defined(_MSC_VER) && !defined(__cplusplus)
  #ifndef bool
    typedef int bool;
    #define true 1
    #define false 0
  #endif
#endif

/*
 * =============================================================================
 * 前向声明
 * =============================================================================
 */
typedef struct MediaPlayer MediaPlayer;
struct FFPlayer;

/*
 * =============================================================================
 * 错误码定义
 * =============================================================================
 */
#define MP_ERR_OK                    0
#define MP_ERR_INVALID_STATE        -1
#define MP_ERR_OUT_OF_MEMORY        -2
#define MP_ERR_INVALID_PARAM        -3
#define MP_ERR_NOT_PREPARED         -4
#define MP_ERR_INTERNAL             -5

/*
 * =============================================================================
 * 播放器状态定义
 * 
 * 状态转换图:
 * 
 *                    ┌──────────────┐
 *                    │ MP_STATE_IDLE │ ◄── mp_create()
 *                    └──────┬───────┘
 *                           │ mp_set_data_source()
 *                    ┌──────▼─────────────┐
 *                    │ MP_STATE_INITIALIZED │
 *                    └──────┬─────────────┘
 *                           │ mp_prepare_async()
 *                    ┌──────▼────────────────┐
 *                    │ MP_STATE_ASYNC_PREPARING │
 *                    └──────┬────────────────┘
 *                           │ FFP_MSG_PREPARED
 *                    ┌──────▼───────────┐
 *                    │ MP_STATE_PREPARED │
 *                    └──────┬───────────┘
 *                           │ mp_start()
 *               ┌───────────▼───────────┐
 *          ┌───►│   MP_STATE_STARTED    │◄───┐
 *          │    └───────────┬───────────┘    │
 *          │                │ mp_pause()     │ mp_start()
 *          │    ┌───────────▼───────────┐    │
 *          │    │   MP_STATE_PAUSED     │────┘
 *          │    └───────────┬───────────┘
 *          │                │ 播放完成
 *          │    ┌───────────▼───────────┐
 *          └────│  MP_STATE_COMPLETED   │
 *               └───────────┬───────────┘
 *                           │ mp_stop()
 *               ┌───────────▼───────────┐
 *               │   MP_STATE_STOPPED    │
 *               └───────────┬───────────┘
 *                           │ mp_release()
 *               ┌───────────▼───────────┐
 *               │     MP_STATE_END      │
 *               └───────────────────────┘
 * =============================================================================
 */

/*-
 * mp_set_data_source()  -> MP_STATE_INITIALIZED
 * mp_reset              -> self
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_IDLE               0

/*-
 * mp_prepare_async()    -> MP_STATE_ASYNC_PREPARING
 * mp_reset              -> MP_STATE_IDLE
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_INITIALIZED        1

/*-
 *                   ... -> MP_STATE_PREPARED
 *                   ... -> MP_STATE_ERROR
 * mp_reset              -> MP_STATE_IDLE
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_ASYNC_PREPARING    2

/*-
 * mp_seek_to()          -> self
 * mp_start()            -> MP_STATE_STARTED
 * mp_reset              -> MP_STATE_IDLE
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_PREPARED           3

/*-
 * mp_seek_to()          -> self
 * mp_start()            -> self
 * mp_pause()            -> MP_STATE_PAUSED
 * mp_stop()             -> MP_STATE_STOPPED
 *                   ... -> MP_STATE_COMPLETED
 *                   ... -> MP_STATE_ERROR
 * mp_reset              -> MP_STATE_IDLE
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_STARTED            4

/*-
 * mp_seek_to()          -> self
 * mp_start()            -> MP_STATE_STARTED
 * mp_pause()            -> self
 * mp_stop()             -> MP_STATE_STOPPED
 * mp_reset              -> MP_STATE_IDLE
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_PAUSED             5

/*-
 * mp_seek_to()          -> self
 * mp_start()            -> MP_STATE_STARTED (from beginning)
 * mp_pause()            -> self
 * mp_stop()             -> MP_STATE_STOPPED
 * mp_reset              -> MP_STATE_IDLE
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_COMPLETED          6

/*-
 * mp_stop()             -> self
 * mp_prepare_async()    -> MP_STATE_ASYNC_PREPARING
 * mp_reset              -> MP_STATE_IDLE
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_STOPPED            7

/*-
 * mp_reset              -> MP_STATE_IDLE
 * mp_release            -> MP_STATE_END
 */
#define MP_STATE_ERROR              8

/*-
 * mp_release            -> self
 */
#define MP_STATE_END                9

/*
 * =============================================================================
 * 选项类别定义
 * =============================================================================
 */
#define MP_OPT_CATEGORY_FORMAT      1   /* AVFormatContext options */
#define MP_OPT_CATEGORY_CODEC       2   /* AVCodecContext options */
#define MP_OPT_CATEGORY_SWS         3   /* SwsContext options */
#define MP_OPT_CATEGORY_PLAYER      4   /* FFPlayer options */
#define MP_OPT_CATEGORY_SWR         5   /* SwrContext options */

/*
 * =============================================================================
 * 全局初始化/反初始化
 * =============================================================================
 */

/**
 * 全局初始化（调用 FFmpeg 初始化）
 */
void mp_global_init(void);

/**
 * 全局反初始化
 */
void mp_global_uninit(void);

/*
 * =============================================================================
 * MediaPlayer 生命周期管理
 * =============================================================================
 */

/**
 * 创建 MediaPlayer 实例
 * @return MediaPlayer 指针，失败返回 NULL
 */
MediaPlayer *mp_create(void);

/**
 * 释放 MediaPlayer 实例
 * @param mp MediaPlayer 实例
 */
void mp_release(MediaPlayer *mp);

/**
 * 增加引用计数
 * @param mp MediaPlayer 实例
 */
void mp_inc_ref(MediaPlayer *mp);

/**
 * 减少引用计数（引用为0时自动释放）
 * @param mp MediaPlayer 实例
 */
void mp_dec_ref(MediaPlayer *mp);

/**
 * 减少引用计数并置空指针
 * @param pmp MediaPlayer 指针的指针
 */
void mp_dec_ref_p(MediaPlayer **pmp);

/**
 * 重置播放器到 IDLE 状态
 * @param mp MediaPlayer 实例
 */
void mp_reset(MediaPlayer *mp);

/**
 * 关闭播放器（可阻塞）
 * @param mp MediaPlayer 实例
 */
void mp_shutdown(MediaPlayer *mp);

/*
 * =============================================================================
 * 窗口/渲染管理
 * =============================================================================
 */

/**
 * 附加到原生窗口
 * @param mp MediaPlayer 实例
 * @param window_handle 原生窗口句柄
 * @param width 窗口宽度
 * @param height 窗口高度
 * @return 0 成功，负数 失败
 */
int mp_attach_window(MediaPlayer *mp, void *window_handle, int width, int height);

/*
 * =============================================================================
 * 播放控制
 * =============================================================================
 */

/**
 * 设置数据源
 * @param mp MediaPlayer 实例
 * @param url 媒体文件路径或 URL
 * @return 0 成功，负数 失败
 */
int mp_set_data_source(MediaPlayer *mp, const char *url);

/**
 * 异步准备播放
 * @param mp MediaPlayer 实例
 * @return 0 成功，负数 失败
 */
int mp_prepare_async(MediaPlayer *mp);

/**
 * 开始播放
 * @param mp MediaPlayer 实例
 * @return 0 成功，负数 失败
 */
int mp_start(MediaPlayer *mp);

/**
 * 暂停播放
 * @param mp MediaPlayer 实例
 * @return 0 成功，负数 失败
 */
int mp_pause(MediaPlayer *mp);

/**
 * 停止播放
 * @param mp MediaPlayer 实例
 * @return 0 成功，负数 失败
 */
int mp_stop(MediaPlayer *mp);

/**
 * 跳转到指定位置
 * @param mp MediaPlayer 实例
 * @param msec 目标位置（毫秒）
 * @return 0 成功，负数 失败
 */
int mp_seek_to(MediaPlayer *mp, long msec);

/**
 * 相对跳转
 * @param mp MediaPlayer 实例
 * @param incr 跳转增量（秒，正=前进，负=后退）
 */
void mp_seek_relative(MediaPlayer *mp, double incr);

/*
 * =============================================================================
 * 状态查询
 * =============================================================================
 */

/**
 * 获取当前状态
 * @param mp MediaPlayer 实例
 * @return 状态值 (MP_STATE_*)
 */
int mp_get_state(MediaPlayer *mp);

/**
 * 是否正在播放
 * @param mp MediaPlayer 实例
 * @return true 正在播放，false 未播放
 */
bool mp_is_playing(MediaPlayer *mp);

/**
 * 获取当前播放位置
 * @param mp MediaPlayer 实例
 * @return 当前位置（毫秒）
 */
long mp_get_current_position(MediaPlayer *mp);

/**
 * 获取媒体总时长
 * @param mp MediaPlayer 实例
 * @return 总时长（毫秒）
 */
long mp_get_duration(MediaPlayer *mp);

/**
 * 获取可播放时长（已缓冲）
 * @param mp MediaPlayer 实例
 * @return 可播放时长（毫秒）
 */
long mp_get_playable_duration(MediaPlayer *mp);

/*
 * =============================================================================
 * 音量控制
 * =============================================================================
 */

/**
 * 设置播放音量
 * @param mp MediaPlayer 实例
 * @param volume 音量 (0.0 - 1.0)
 */
void mp_set_volume(MediaPlayer *mp, float volume);

/**
 * 获取播放音量
 * @param mp MediaPlayer 实例
 * @return 音量 (0.0 - 1.0)
 */
float mp_get_volume(MediaPlayer *mp);

/**
 * 调节音量
 * @param mp MediaPlayer 实例
 * @param sign 方向 (1=增加, -1=减少)
 * @param step 步长
 */
void mp_update_volume(MediaPlayer *mp, int sign, double step);

/**
 * 切换静音状态
 * @param mp MediaPlayer 实例
 */
void mp_toggle_mute(MediaPlayer *mp);

/**
 * 设置静音
 * @param mp MediaPlayer 实例
 * @param mute 1=静音, 0=取消静音
 */
void mp_set_mute(MediaPlayer *mp, int mute);

/**
 * 是否静音
 * @param mp MediaPlayer 实例
 * @return 1=静音, 0=非静音
 */
int mp_is_muted(MediaPlayer *mp);

/*
 * =============================================================================
 * 播放速率控制
 * =============================================================================
 */

/**
 * 设置播放速率
 * @param mp MediaPlayer 实例
 * @param rate 播放速率 (0.5 ~ 2.0)
 * @return 0 成功，负数 失败
 */
int mp_set_playback_rate(MediaPlayer *mp, float rate);

/**
 * 获取当前播放速率
 * @param mp MediaPlayer 实例
 * @return 播放速率 (默认 1.0)
 */
float mp_get_playback_rate(MediaPlayer *mp);

/*
 * =============================================================================
 * 循环控制
 * =============================================================================
 */

/**
 * 设置循环次数
 * @param mp MediaPlayer 实例
 * @param loop 循环次数 (0=无限循环, 1=不循环, >1=循环次数)
 */
void mp_set_loop(MediaPlayer *mp, int loop);

/**
 * 获取循环次数
 * @param mp MediaPlayer 实例
 * @return 循环次数
 */
int mp_get_loop(MediaPlayer *mp);

/*
 * =============================================================================
 * 选项设置
 * =============================================================================
 */

/**
 * 设置字符串选项
 * @param mp MediaPlayer 实例
 * @param opt_category 选项类别 (MP_OPT_CATEGORY_*)
 * @param name 选项名
 * @param value 选项值
 */
void mp_set_option(MediaPlayer *mp, int opt_category, const char *name, const char *value);

/**
 * 设置整数选项
 * @param mp MediaPlayer 实例
 * @param opt_category 选项类别 (MP_OPT_CATEGORY_*)
 * @param name 选项名
 * @param value 选项值
 */
void mp_set_option_int(MediaPlayer *mp, int opt_category, const char *name, int64_t value);

/*
 * =============================================================================
 * 属性访问
 * =============================================================================
 */

/**
 * 获取浮点属性
 * @param mp MediaPlayer 实例
 * @param id 属性 ID
 * @param default_value 默认值
 * @return 属性值
 */
float mp_get_property_float(MediaPlayer *mp, int id, float default_value);

/**
 * 设置浮点属性
 * @param mp MediaPlayer 实例
 * @param id 属性 ID
 * @param value 属性值
 */
void mp_set_property_float(MediaPlayer *mp, int id, float value);

/**
 * 获取整数属性
 * @param mp MediaPlayer 实例
 * @param id 属性 ID
 * @param default_value 默认值
 * @return 属性值
 */
int64_t mp_get_property_int64(MediaPlayer *mp, int id, int64_t default_value);

/**
 * 设置整数属性
 * @param mp MediaPlayer 实例
 * @param id 属性 ID
 * @param value 属性值
 */
void mp_set_property_int64(MediaPlayer *mp, int id, int64_t value);

/*
 * =============================================================================
 * 编解码器信息
 * =============================================================================
 */

/**
 * 获取视频编解码器信息
 * @param mp MediaPlayer 实例
 * @param codec_info 输出编解码器信息字符串（需要调用者 free）
 * @return 0 成功，负数 失败
 */
int mp_get_video_codec_info(MediaPlayer *mp, char **codec_info);

/**
 * 获取音频编解码器信息
 * @param mp MediaPlayer 实例
 * @param codec_info 输出编解码器信息字符串（需要调用者 free）
 * @return 0 成功，负数 失败
 */
int mp_get_audio_codec_info(MediaPlayer *mp, char **codec_info);

/*
 * =============================================================================
 * 消息队列（内部使用，暂不向上层传递）
 * =============================================================================
 */

/**
 * 获取消息（ijkplayer 风格的过滤+处理函数）
 * 
 * 这个函数是 ijkplayer 的核心设计：
 * - 从消息队列取消息
 * - 内部处理所有消息（状态更新、执行 FFPlayer 操作）
 * - 过滤掉内部请求消息（FFP_REQ_*），继续取下一条
 * - 只返回通知消息（FFP_MSG_*）给上层
 * 
 * 上层（Qt/Java/iOS）应该在独立线程中循环调用此函数来驱动消息处理
 * 
 * @param mp MediaPlayer 实例
 * @param msg 输出消息（只返回 FFP_MSG_* 消息）
 * @param block 是否阻塞等待
 * @return < 0 中止, 0 无消息, > 0 有消息
 */
int mp_get_msg(MediaPlayer *mp, AVMessage *msg, int block);

/**
 * 中止消息队列（唤醒阻塞的 mp_get_msg）
 * @param mp MediaPlayer 实例
 */
void mp_abort_msg_queue(MediaPlayer *mp);

/*
 * =============================================================================
 * 用户数据
 * =============================================================================
 */

/**
 * 获取用户数据指针
 * @param mp MediaPlayer 实例
 * @return 用户数据指针
 */
void *mp_get_weak_thiz(MediaPlayer *mp);

/**
 * 设置用户数据指针
 * @param mp MediaPlayer 实例
 * @param weak_thiz 用户数据指针
 * @return 原用户数据指针
 */
void *mp_set_weak_thiz(MediaPlayer *mp, void *weak_thiz);


/*
 * =============================================================================
 * 渲染模式设置
 * =============================================================================
 */

/**
 * 设置渲染模式
 * @param mp MediaPlayer 实例
 * @param mode 渲染模式 (FFP_RENDER_MODE_SDL 或 FFP_RENDER_MODE_CALLBACK)
 */
void mp_set_render_mode(MediaPlayer *mp, FFPRenderMode mode);

/**
 * 获取渲染模式
 * @param mp MediaPlayer 实例
 * @return 当前渲染模式
 */
FFPRenderMode mp_get_render_mode(MediaPlayer *mp);

/**
 * 设置视频帧回调函数
 * 
 * 当 render_mode 为 FFP_RENDER_MODE_CALLBACK 时，每当有新帧需要显示时，
 * 将调用此回调函数。回调在内部渲染线程中调用，需要注意线程安全。
 * 
 * 帧数据在回调返回后可能失效，如果需要保留数据，请在回调中复制。
 * 
 * @param mp MediaPlayer 实例
 * @param cb 回调函数
 * @param opaque 传递给回调的用户数据
 */
void mp_set_video_frame_callback(MediaPlayer *mp, ffp_video_frame_callback cb, void *opaque);

/*
 * =============================================================================
 * 内部使用 - FFPlayer 访问
 * =============================================================================
 */

/**
 * 获取内部 FFPlayer 指针（仅供高级用途）
 * @param mp MediaPlayer 实例
 * @return FFPlayer 指针
 */
struct FFPlayer *mp_get_ffplayer(MediaPlayer *mp);

#ifdef __cplusplus
}
#endif

#endif /* MEDIAPLAYER_H */

