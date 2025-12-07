/*
 * ff_ffmsg_queue.h
 *      Message queue implementation for MediaPlayer
 *      Based on ijkplayer's ff_ffmsg_queue.h
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

#ifndef FF_FFMSG_QUEUE_H
#define FF_FFMSG_QUEUE_H

#include <assert.h>
#include "ff_define.h"
#include "ff_ffmsg.h"

/*
 * =============================================================================
 * 消息结构体
 * =============================================================================
 */
typedef struct AVMessage {
    int what;                       /* 消息类型 */
    int arg1;                       /* 参数1 */
    int arg2;                       /* 参数2 */
    void *obj;                      /* 附加对象 */
    void (*free_l)(void *obj);      /* 对象释放函数 */
    struct AVMessage *next;         /* 链表下一个节点 */
} AVMessage;

/*
 * =============================================================================
 * 消息队列结构体
 * =============================================================================
 */
typedef struct MessageQueue {
    AVMessage *first_msg;           /* 队列头 */
    AVMessage *last_msg;            /* 队列尾 */
    int nb_messages;                /* 消息数量 */
    int abort_request;              /* 中止请求标志 */
    SDL_mutex *mutex;               /* 互斥锁 */
    SDL_cond *cond;                 /* 条件变量 */

    AVMessage *recycle_msg;         /* 回收消息链表 */
    int recycle_count;              /* 回收计数 */
    int alloc_count;                /* 分配计数 */
} MessageQueue;

/*
 * =============================================================================
 * 消息释放函数
 * =============================================================================
 */
inline static void msg_free_res(AVMessage *msg)
{
    if (!msg || !msg->obj)
        return;
    assert(msg->free_l);
    msg->free_l(msg->obj);
    msg->obj = NULL;
}

/*
 * =============================================================================
 * 内部消息入队函数（需要外部加锁）
 * =============================================================================
 */
inline static int msg_queue_put_private(MessageQueue *q, AVMessage *msg)
{
    AVMessage *msg1;

    if (q->abort_request)
        return -1;

    /* 优先从回收链表获取消息节点 */
    msg1 = q->recycle_msg;
    if (msg1) {
        q->recycle_msg = msg1->next;
        q->recycle_count++;
    } else {
        q->alloc_count++;
        msg1 = (AVMessage *)av_malloc(sizeof(AVMessage));
    }

    if (!msg1)
        return -1;

    *msg1 = *msg;
    msg1->next = NULL;

    if (!q->last_msg)
        q->first_msg = msg1;
    else
        q->last_msg->next = msg1;
    q->last_msg = msg1;
    q->nb_messages++;
    SDL_CondSignal(q->cond);
    return 0;
}

/*
 * =============================================================================
 * 消息入队（线程安全）
 * =============================================================================
 */
inline static int msg_queue_put(MessageQueue *q, AVMessage *msg)
{
    int ret;

    SDL_LockMutex(q->mutex);
    ret = msg_queue_put_private(q, msg);
    SDL_UnlockMutex(q->mutex);

    return ret;
}

/*
 * =============================================================================
 * 初始化消息结构体
 * =============================================================================
 */
inline static void msg_init_msg(AVMessage *msg)
{
    memset(msg, 0, sizeof(AVMessage));
}

/*
 * =============================================================================
 * 便捷入队函数 - 无参数
 * =============================================================================
 */
inline static void msg_queue_put_simple1(MessageQueue *q, int what)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg_queue_put(q, &msg);
}

/*
 * =============================================================================
 * 便捷入队函数 - 1个参数
 * =============================================================================
 */
inline static void msg_queue_put_simple2(MessageQueue *q, int what, int arg1)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg_queue_put(q, &msg);
}

/*
 * =============================================================================
 * 便捷入队函数 - 2个参数
 * =============================================================================
 */
inline static void msg_queue_put_simple3(MessageQueue *q, int what, int arg1, int arg2)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg.arg2 = arg2;
    msg_queue_put(q, &msg);
}

/*
 * =============================================================================
 * 默认对象释放函数
 * =============================================================================
 */
inline static void msg_obj_free_l(void *obj)
{
    av_free(obj);
}

/*
 * =============================================================================
 * 便捷入队函数 - 带对象
 * =============================================================================
 */
inline static void msg_queue_put_simple4(MessageQueue *q, int what, int arg1, int arg2, void *obj, int obj_len)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg.arg2 = arg2;
    msg.obj = av_malloc(obj_len);
    memcpy(msg.obj, obj, obj_len);
    msg.free_l = msg_obj_free_l;
    msg_queue_put(q, &msg);
}

/*
 * =============================================================================
 * 初始化消息队列
 * =============================================================================
 */
inline static void msg_queue_init(MessageQueue *q)
{
    memset(q, 0, sizeof(MessageQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
    q->abort_request = 1;  /* 初始状态为中止 */
}

/*
 * =============================================================================
 * 清空消息队列（将消息移到回收链表）
 * =============================================================================
 */
inline static void msg_queue_flush(MessageQueue *q)
{
    AVMessage *msg, *msg1;

    SDL_LockMutex(q->mutex);
    for (msg = q->first_msg; msg != NULL; msg = msg1) {
        msg1 = msg->next;
        msg_free_res(msg);
        msg->next = q->recycle_msg;
        q->recycle_msg = msg;
    }
    q->last_msg = NULL;
    q->first_msg = NULL;
    q->nb_messages = 0;
    SDL_UnlockMutex(q->mutex);
}

/*
 * =============================================================================
 * 销毁消息队列
 * =============================================================================
 */
inline static void msg_queue_destroy(MessageQueue *q)
{
    msg_queue_flush(q);

    SDL_LockMutex(q->mutex);
    while (q->recycle_msg) {
        AVMessage *msg = q->recycle_msg;
        if (msg)
            q->recycle_msg = msg->next;
        msg_free_res(msg);
        av_freep(&msg);
    }
    SDL_UnlockMutex(q->mutex);

    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

/*
 * =============================================================================
 * 中止消息队列
 * =============================================================================
 */
inline static void msg_queue_abort(MessageQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 1;
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
}

/*
 * =============================================================================
 * 启动消息队列
 * =============================================================================
 */
inline static void msg_queue_start(MessageQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;

    /* 发送一条刷新消息 */
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = FFP_MSG_FLUSH;
    msg_queue_put_private(q, &msg);
    SDL_UnlockMutex(q->mutex);
}

/*
 * =============================================================================
 * 获取消息（阻塞或非阻塞）
 * @return < 0 中止, 0 无消息, > 0 有消息
 * =============================================================================
 */
inline static int msg_queue_get(MessageQueue *q, AVMessage *msg, int block)
{
    AVMessage *msg1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        msg1 = q->first_msg;
        if (msg1) {
            q->first_msg = msg1->next;
            if (!q->first_msg)
                q->last_msg = NULL;
            q->nb_messages--;
            *msg = *msg1;
            msg1->obj = NULL;
            /* 放入回收链表 */
            msg1->next = q->recycle_msg;
            q->recycle_msg = msg1;
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

/*
 * =============================================================================
 * 移除指定类型的消息
 * =============================================================================
 */
inline static void msg_queue_remove(MessageQueue *q, int what)
{
    AVMessage **p_msg, *msg, *last_msg;
    SDL_LockMutex(q->mutex);

    last_msg = q->first_msg;

    if (!q->abort_request && q->first_msg) {
        p_msg = &q->first_msg;
        while (*p_msg) {
            msg = *p_msg;

            if (msg->what == what) {
                *p_msg = msg->next;
                msg_free_res(msg);
                msg->next = q->recycle_msg;
                q->recycle_msg = msg;
                q->nb_messages--;
            } else {
                last_msg = msg;
                p_msg = &msg->next;
            }
        }

        if (q->first_msg) {
            q->last_msg = last_msg;
        } else {
            q->last_msg = NULL;
        }
    }

    SDL_UnlockMutex(q->mutex);
}

#endif /* FF_FFMSG_QUEUE_H */

