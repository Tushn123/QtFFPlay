# MediaPlayer 异步设计修改建议

> **状态**: ✅ 已完成修改

## 修改完成清单

| 文件 | 修改内容 | 状态 |
|------|----------|------|
| `ff_ffplayer.h` | 添加 `ext_msg_queue` 指针 | ✅ |
| `mediaplayer.c` | `mp_create()` 建立 FFPlayer 到消息队列的关联 | ✅ |
| `mediaplayer.c` | 实现 `ffp_notify_msg1/2/3()` 函数 | ✅ |
| `mediaplayer.c` | 移除 `mp_prepare_async_l()` 中立即发送 PREPARED 的代码 | ✅ |
| `ff_ffplayer.c` | 在 `ffp_prepare_async()` 中发送 `FFP_MSG_PREPARED` | ✅ |
| `ffplay.c` | 在 `read_thread` 结束时发送 `FFP_MSG_COMPLETED` | ✅ |
| `ffplay.c` | 在 `read_thread` 失败时发送 `FFP_MSG_ERROR` | ✅ |
| `ffplay.c` | 在 `video_open()` 中发送 `FFP_MSG_VIDEO_SIZE_CHANGED` | ✅ |
| `ffplay.c` | 在首帧渲染时发送 `FFP_MSG_VIDEO_RENDERING_START` | ✅ |

---

## 一、ijkplayer 异步设计分析

### 1.1 为什么需要异步设计？

播放器的某些操作可能耗时较长，如果在 UI 线程同步执行会导致界面卡顿：

| 操作 | 耗时原因 | 是否需要异步 |
|------|----------|--------------|
| `start` | 需要启动音视频解码、渲染线程 | ✅ 是 |
| `pause` | 需要同步暂停多个线程的时钟 | ✅ 是 |
| `seek` | 需要清空缓冲区、定位关键帧、等待解码 | ✅ 是 |
| `prepare_async` | 网络连接、探测流格式、打开解码器 | ✅ 是 |
| `stop` | 需要等待各线程退出 | ⚠️ 可选（ijkplayer 是同步） |
| `set_data_source` | 仅设置 URL 字符串 | ❌ 否 |
| `get_duration` | 读取已有数据 | ❌ 否 |
| `set_volume` | 直接设置变量 | ❌ 否 |

### 1.2 ijkplayer 的异步模式

```
┌─────────────────────────────────────────────────────────────────┐
│                         UI 线程                                  │
│                                                                 │
│  mp_start() ──► 状态检查 ──► 发送 FFP_REQ_START 到消息队列      │
│                              │                                  │
│                              ▼ (立即返回，不阻塞 UI)             │
└─────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                      消息循环线程                                │
│                                                                 │
│  mp_get_msg() ──► 取出 FFP_REQ_START                           │
│       │                                                         │
│       ▼                                                         │
│  ffp_start() ──► 实际执行播放操作                               │
│       │                                                         │
│       ▼                                                         │
│  更新状态机 ──► MP_STATE_STARTED                                │
│       │                                                         │
│       ▼                                                         │
│  continue; (不返回给上层，继续取下一条消息)                      │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 ijkplayer 中需要异步的操作

#### ① start (异步)
```c
// ijkplayer.c - ijkmp_start_l
static int ijkmp_start_l(IjkMediaPlayer *mp)
{
    MP_RET_IF_FAILED(ikjmp_chkst_start_l(mp->mp_state));
    
    // "移除再添加"防抖模式
    ffp_remove_msg(mp->ffplayer, FFP_REQ_START);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_PAUSE);
    ffp_notify_msg1(mp->ffplayer, FFP_REQ_START);  // 只发消息，不执行
    
    return 0;  // 立即返回
}
```

#### ② pause (异步)
```c
// ijkplayer.c - ijkmp_pause_l
static int ijkmp_pause_l(IjkMediaPlayer *mp)
{
    MP_RET_IF_FAILED(ikjmp_chkst_pause_l(mp->mp_state));
    
    ffp_remove_msg(mp->ffplayer, FFP_REQ_START);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_PAUSE);
    ffp_notify_msg1(mp->ffplayer, FFP_REQ_PAUSE);  // 只发消息，不执行
    
    return 0;
}
```

#### ③ seek (异步)
```c
// ijkplayer.c - ijkmp_seek_to_l
int ijkmp_seek_to_l(IjkMediaPlayer *mp, long msec)
{
    MP_RET_IF_FAILED(ikjmp_chkst_seek_l(mp->mp_state));
    
    mp->seek_req = 1;
    mp->seek_msec = msec;
    ffp_remove_msg(mp->ffplayer, FFP_REQ_SEEK);
    ffp_notify_msg2(mp->ffplayer, FFP_REQ_SEEK, (int)msec);  // 只发消息
    
    return 0;
}
```

#### ④ prepare_async (异步)
```c
// ijkplayer.c - ijkmp_prepare_async_l
static int ijkmp_prepare_async_l(IjkMediaPlayer *mp)
{
    // 状态检查 ...
    
    ijkmp_change_state_l(mp, MP_STATE_ASYNC_PREPARING);
    msg_queue_start(&mp->ffplayer->msg_queue);
    
    // 创建消息循环线程
    ijkmp_inc_ref(mp);
    mp->msg_thread = SDL_CreateThreadEx(&mp->_msg_thread, ijkmp_msg_loop, mp, "ff_msg_loop");
    
    // 调用 FFPlayer 准备（这会启动 read_thread）
    int retval = ffp_prepare_async_l(mp->ffplayer, mp->data_source);
    if (retval < 0) {
        ijkmp_change_state_l(mp, MP_STATE_ERROR);
        return retval;
    }
    
    // 注意：FFP_MSG_PREPARED 由 read_thread 发送，不是这里
    return 0;
}
```

#### ⑤ stop (同步)
```c
// ijkplayer.c - ijkmp_stop_l
static int ijkmp_stop_l(IjkMediaPlayer *mp)
{
    // 状态检查 ...
    
    ffp_remove_msg(mp->ffplayer, FFP_REQ_START);
    ffp_remove_msg(mp->ffplayer, FFP_REQ_PAUSE);
    int retval = ffp_stop_l(mp->ffplayer);  // 直接调用，同步等待
    if (retval < 0) {
        return retval;
    }
    
    ijkmp_change_state_l(mp, MP_STATE_STOPPED);
    return 0;
}
```

**注意**：stop 是同步的，因为通常在 stop 之后需要确保资源已释放。

---

## 二、mediaplayer.c 当前实现分析

### 2.1 已正确实现异步的操作

| 操作 | 当前实现 | 状态 |
|------|----------|------|
| `mp_start_l` | 发送 `FFP_REQ_START` 消息 | ✅ 正确 |
| `mp_pause_l` | 发送 `FFP_REQ_PAUSE` 消息 | ✅ 正确 |
| `mp_seek_to_l` | 发送 `FFP_REQ_SEEK` 消息 | ✅ 正确 |
| `mp_stop_l` | 直接调用 `ffp_stop` | ✅ 正确（同步设计） |

### 2.2 需要修改的问题

#### 问题 1：`ffp_notify_msg` 函数是空实现

```c
// mediaplayer.c - 当前代码
void ffp_notify_msg1(FFPlayer *ffp, int what)
{
    /* TODO: 需要建立 FFPlayer 到 MediaPlayer 的关联 */
    (void)ffp;
    (void)what;
}
```

**问题**：FFPlayer 层无法向 MediaPlayer 发送消息，导致：
- `FFP_MSG_PREPARED` 无法从 read_thread 发送
- `FFP_MSG_COMPLETED` 无法从 read_thread 发送
- `FFP_MSG_ERROR` 无法发送
- 所有播放层事件无法通知上层

#### 问题 2：`mp_prepare_async_l` 立即发送 `FFP_MSG_PREPARED`

```c
// mediaplayer.c - 当前代码（问题）
static int mp_prepare_async_l(MediaPlayer *mp)
{
    // ...
    int ret = ffp_prepare_async(mp->ffplayer, mp->data_source);
    if (ret < 0) {
        // ...
    }
    
    // ❌ 错误：立即发送 PREPARED 消息
    // 实际上应该由 FFPlayer 的 read_thread 在完成后发送
    av_log(NULL, AV_LOG_INFO, "[MediaPlayer] FFPlayer prepared, sending FFP_MSG_PREPARED\n");
    msg_queue_put_simple1(&mp->msg_queue, FFP_MSG_PREPARED);
    
    return 0;
}
```

**问题**：prepare 是异步操作，可能需要几秒钟（网络流），不应该立即发送 PREPARED。

#### 问题 3：消息队列位置不一致

```c
// ijkplayer: msg_queue 在 FFPlayer 中
int retval = msg_queue_get(&mp->ffplayer->msg_queue, msg, block);

// mediaplayer.c: msg_queue 在 MediaPlayer 中
int retval = msg_queue_get(&mp->msg_queue, msg, block);
```

这个设计差异本身没问题，但需要确保 `ffp_notify_msg` 能访问正确的队列。

---

## 三、修改建议

### 3.1 建立 FFPlayer 到 MediaPlayer 的关联

**方案 A：在 FFPlayer 中保存 MediaPlayer 指针**

```c
// ff_ffplayer.h
struct FFPlayer {
    // ... 其他成员
    void *opaque;  // 指向 MediaPlayer
    MessageQueue *ext_msg_queue;  // 指向外部消息队列
};
```

```c
// mediaplayer.c - mp_create()
MediaPlayer *mp_create(void)
{
    MediaPlayer *mp = ...;
    mp->ffplayer = ffp_create();
    
    // 建立关联
    mp->ffplayer->opaque = mp;
    mp->ffplayer->ext_msg_queue = &mp->msg_queue;
    
    return mp;
}
```

**方案 B：使用回调函数（更解耦）**

```c
// ff_ffplayer.h
typedef void (*ffp_msg_callback)(void *opaque, int what, int arg1, int arg2);

struct FFPlayer {
    void *msg_opaque;
    ffp_msg_callback msg_callback;
};
```

### 3.2 实现 ffp_notify_msg 函数

```c
// mediaplayer.c
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
```

### 3.3 修改 mp_prepare_async_l

```c
static int mp_prepare_async_l(MediaPlayer *mp)
{
    assert(mp);
    
    /* 状态检查 */
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ASYNC_PREPARING);
    // ... 其他状态检查
    
    if (!mp->data_source)
        return MP_ERR_INVALID_PARAM;
    
    mp_change_state_l(mp, MP_STATE_ASYNC_PREPARING);
    
    /* 启动消息队列 */
    msg_queue_start(&mp->msg_queue);
    
    /* 调用 FFPlayer 准备 */
    int ret = ffp_prepare_async(mp->ffplayer, mp->data_source);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "[MediaPlayer] ffp_prepare_async failed: %d\n", ret);
        mp_change_state_l(mp, MP_STATE_ERROR);
        return ret;
    }
    
    /* 
     * 重要：不在这里发送 FFP_MSG_PREPARED！
     * FFP_MSG_PREPARED 应该由 FFPlayer 的 read_thread 在完成流打开后发送
     * 通过 ffp_notify_msg1(ffp, FFP_MSG_PREPARED)
     */
    
    return 0;
}
```

### 3.4 在 ff_ffplayer.c 中添加消息发送

需要在 FFPlayer 的关键位置添加消息通知：

```c
// ff_ffplayer.c - read_thread 或 stream_open 完成后
static int read_thread(void *arg)
{
    FFPlayer *ffp = arg;
    // ...
    
    /* 打开输入 */
    ffp_notify_msg1(ffp, FFP_MSG_OPEN_INPUT);
    ret = avformat_open_input(&ic, is->filename, ...);
    if (ret < 0) {
        ffp_notify_msg2(ffp, FFP_MSG_ERROR, ret);
        goto fail;
    }
    
    /* 获取流信息 */
    ffp_notify_msg1(ffp, FFP_MSG_FIND_STREAM_INFO);
    ret = avformat_find_stream_info(ic, NULL);
    // ...
    
    /* 打开解码器 */
    ffp_notify_msg1(ffp, FFP_MSG_COMPONENT_OPEN);
    // ...
    
    /* 准备完成 */
    ffp_notify_msg1(ffp, FFP_MSG_PREPARED);
    
    /* 主读取循环 */
    for (;;) {
        // ...
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);
            } else {
                ffp_notify_msg2(ffp, FFP_MSG_ERROR, ret);
            }
            break;
        }
    }
    
fail:
    // ...
}
```

---

## 四、完整修改清单

### 4.1 ff_ffplayer.h 修改

```c
// 添加外部消息队列指针
struct FFPlayer {
    // ... 现有成员
    
    /* 外部消息队列（指向 MediaPlayer 的 msg_queue）*/
    MessageQueue *ext_msg_queue;
};
```

### 4.2 mediaplayer.c 修改

#### 修改 mp_create()
```c
MediaPlayer *mp_create(void)
{
    MediaPlayer *mp = (MediaPlayer *)calloc(1, sizeof(MediaPlayer));
    if (!mp) return NULL;
    
    pthread_mutex_init(&mp->mutex, NULL);
    
    mp->ffplayer = ffp_create();
    if (!mp->ffplayer) {
        pthread_mutex_destroy(&mp->mutex);
        free(mp);
        return NULL;
    }
    
    msg_queue_init(&mp->msg_queue);
    
    // ★ 关键：建立 FFPlayer 到 MediaPlayer 消息队列的关联
    mp->ffplayer->ext_msg_queue = &mp->msg_queue;
    
    mp->mp_state = MP_STATE_IDLE;
    mp->ref_count = 1;
    
    return mp;
}
```

#### 修改 ffp_notify_msg 函数
```c
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
```

#### 修改 mp_prepare_async_l()
```c
static int mp_prepare_async_l(MediaPlayer *mp)
{
    assert(mp);
    
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_IDLE);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ASYNC_PREPARING);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PREPARED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_STARTED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_PAUSED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_COMPLETED);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_ERROR);
    MPST_RET_IF_EQ(mp->mp_state, MP_STATE_END);
    
    if (!mp->data_source)
        return MP_ERR_INVALID_PARAM;
    
    mp_change_state_l(mp, MP_STATE_ASYNC_PREPARING);
    msg_queue_start(&mp->msg_queue);
    
    int ret = ffp_prepare_async(mp->ffplayer, mp->data_source);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "[MediaPlayer] ffp_prepare_async failed: %d\n", ret);
        mp_change_state_l(mp, MP_STATE_ERROR);
        return ret;
    }
    
    /* 
     * 不发送 FFP_MSG_PREPARED！
     * 由 FFPlayer 内部在适当时机发送
     */
    
    return 0;
}
```

### 4.3 ff_ffplayer.c 修改

在 FFPlayer 的关键位置添加消息通知（参考 ijkplayer 的 ff_ffplay.c）：

```c
// 在流打开成功后
ffp_notify_msg1(ffp, FFP_MSG_PREPARED);

// 在播放完成时
ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);

// 在发生错误时
ffp_notify_msg2(ffp, FFP_MSG_ERROR, error_code);

// 在视频尺寸变化时
ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, width, height);

// 在 seek 完成时
ffp_notify_msg3(ffp, FFP_MSG_SEEK_COMPLETE, seek_pos, ret);

// 其他事件...
```

---

## 五、操作异步性总结

| 操作 | UI 线程行为 | 消息线程行为 | 状态变化时机 |
|------|-------------|--------------|--------------|
| `mp_start` | 发送 `FFP_REQ_START` 并立即返回 | 调用 `ffp_start`，更新状态为 `STARTED` | 消息处理时 |
| `mp_pause` | 发送 `FFP_REQ_PAUSE` 并立即返回 | 调用 `ffp_pause`，更新状态为 `PAUSED` | 消息处理时 |
| `mp_seek_to` | 发送 `FFP_REQ_SEEK` 并立即返回 | 调用 `ffp_seek_to` | 消息处理时 |
| `mp_prepare_async` | 调用 `ffp_prepare_async` 并立即返回 | 收到 `FFP_MSG_PREPARED` 时更新状态 | FFPlayer 发送消息时 |
| `mp_stop` | 直接调用 `ffp_stop`（同步等待） | - | 调用完成时 |

---

## 六、测试建议

1. **测试 start/pause 快速切换**：连续点击 start/pause，验证防抖逻辑
2. **测试 seek 连续操作**：快速拖动进度条，验证只执行最后一次 seek
3. **测试网络流 prepare**：使用 HTTP 流验证 PREPARED 消息时机
4. **测试错误处理**：使用无效 URL 验证 ERROR 消息
5. **测试播放完成**：播放到结尾验证 COMPLETED 消息

