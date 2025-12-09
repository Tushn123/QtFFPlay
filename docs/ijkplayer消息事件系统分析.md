# ijkplayer 消息事件系统分析

## 概述

ijkplayer 采用消息队列机制实现播放层（FFPlayer/ffplay）与上层（JNI/Java）之间的异步通信。消息分为两类：
- **FFP_REQ_*** - 内部请求消息（由 IjkMediaPlayer 产生，在 ijkmp_get_msg 内部消费）
- **FFP_MSG_*** - 外部通知消息（由 FFPlayer/ffplay 产生，传递到 JNI/Java 层）

## 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         Java Layer                               │
│    IjkMediaPlayer.postEventFromNative(what, arg1, arg2, obj)    │
└─────────────────────────────────────────────────────────────────┘
                                ▲
                                │ JNI post_event()
┌─────────────────────────────────────────────────────────────────┐
│                    message_loop_n (JNI)                          │
│              处理 FFP_MSG_* 消息，转换为 MEDIA_* 事件             │
└─────────────────────────────────────────────────────────────────┘
                                ▲
                                │ ijkmp_get_msg()
┌─────────────────────────────────────────────────────────────────┐
│                    ijkmp_get_msg (IjkMediaPlayer)                │
│         • 消费 FFP_REQ_* 请求（不传递给上层）                    │
│         • 过滤并传递 FFP_MSG_* 消息给 message_loop_n             │
└─────────────────────────────────────────────────────────────────┘
                                ▲
                                │ msg_queue_get()
┌─────────────────────────────────────────────────────────────────┐
│                   ffplayer->msg_queue                            │
│                      消息队列                                     │
└─────────────────────────────────────────────────────────────────┘
                                ▲
                                │ ffp_notify_msg()
┌─────────────────────────────────────────────────────────────────┐
│              FFPlayer / ffplay (ff_ffplay.c)                     │
│                   消息生产者                                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 一、消息类型定义 (ff_ffmsg.h)

### 1.1 外部通知消息 (FFP_MSG_*)

| 消息常量 | 值 | 参数说明 | 描述 |
|----------|-----|----------|------|
| `FFP_MSG_FLUSH` | 0 | 无 | 刷新消息队列 |
| `FFP_MSG_ERROR` | 100 | arg1=错误码 | 播放错误 |
| `FFP_MSG_PREPARED` | 200 | 无 | 播放器准备完成 |
| `FFP_MSG_COMPLETED` | 300 | 无 | 播放完成 |
| `FFP_MSG_VIDEO_SIZE_CHANGED` | 400 | arg1=宽度, arg2=高度 | 视频尺寸变化 |
| `FFP_MSG_SAR_CHANGED` | 401 | arg1=sar.num, arg2=sar.den | 像素宽高比变化 |
| `FFP_MSG_VIDEO_RENDERING_START` | 402 | 无 | 视频首帧渲染开始 |
| `FFP_MSG_AUDIO_RENDERING_START` | 403 | 无 | 音频首帧渲染开始 |
| `FFP_MSG_VIDEO_ROTATION_CHANGED` | 404 | arg1=旋转角度 | 视频旋转角度变化 |
| `FFP_MSG_AUDIO_DECODED_START` | 405 | 无 | 音频解码开始 |
| `FFP_MSG_VIDEO_DECODED_START` | 406 | 无 | 视频解码开始 |
| `FFP_MSG_OPEN_INPUT` | 407 | 无 | 开始打开输入源 |
| `FFP_MSG_FIND_STREAM_INFO` | 408 | 无 | 开始获取流信息 |
| `FFP_MSG_COMPONENT_OPEN` | 409 | 无 | 开始打开解码器组件 |
| `FFP_MSG_VIDEO_SEEK_RENDERING_START` | 410 | arg1=是否精确seek | seek后视频首帧渲染 |
| `FFP_MSG_AUDIO_SEEK_RENDERING_START` | 411 | arg1=是否精确seek | seek后音频首帧渲染 |
| `FFP_MSG_BUFFERING_START` | 500 | arg1=是否首次缓冲 | 缓冲开始 |
| `FFP_MSG_BUFFERING_END` | 501 | arg1=是否首次缓冲 | 缓冲结束 |
| `FFP_MSG_BUFFERING_UPDATE` | 502 | arg1=缓冲位置(ms), arg2=百分比 | 缓冲进度更新 |
| `FFP_MSG_BUFFERING_BYTES_UPDATE` | 503 | arg1=缓冲字节数, arg2=高水位 | 缓冲字节更新 |
| `FFP_MSG_BUFFERING_TIME_UPDATE` | 504 | arg1=缓冲时长(ms), arg2=高水位 | 缓冲时长更新 |
| `FFP_MSG_SEEK_COMPLETE` | 600 | arg1=seek位置, arg2=错误码 | seek完成 |
| `FFP_MSG_PLAYBACK_STATE_CHANGED` | 700 | 无 | 播放状态变化 |
| `FFP_MSG_TIMED_TEXT` | 800 | obj=字幕文本 | 字幕更新 |
| `FFP_MSG_ACCURATE_SEEK_COMPLETE` | 900 | arg1=当前位置(ms) | 精确seek完成 |
| `FFP_MSG_GET_IMG_STATE` | 1000 | arg1=时间戳, arg2=结果码, obj=文件名 | 截图状态 |
| `FFP_MSG_VIDEO_DECODER_OPEN` | 10001 | 无 | 视频解码器打开 |

### 1.2 内部请求消息 (FFP_REQ_*)

| 消息常量 | 值 | 参数说明 | 描述 |
|----------|-----|----------|------|
| `FFP_REQ_START` | 20001 | 无 | 请求开始播放 |
| `FFP_REQ_PAUSE` | 20002 | 无 | 请求暂停播放 |
| `FFP_REQ_SEEK` | 20003 | arg1=目标位置(ms) | 请求seek |

---

## 二、消息产生位置详解 (ff_ffplay.c)

### 2.1 准备阶段消息

#### FFP_MSG_OPEN_INPUT
```c
// read_thread() - 第 3128 行
ffp_notify_msg1(ffp, FFP_MSG_OPEN_INPUT);
```
**触发时机**：开始调用 `avformat_open_input()` 打开输入源之前

#### FFP_MSG_FIND_STREAM_INFO
```c
// read_thread() - 第 3171 行
ffp_notify_msg1(ffp, FFP_MSG_FIND_STREAM_INFO);
```
**触发时机**：开始调用 `avformat_find_stream_info()` 获取流信息之前

#### FFP_MSG_COMPONENT_OPEN
```c
// read_thread() - 第 3293 行
ffp_notify_msg1(ffp, FFP_MSG_COMPONENT_OPEN);
```
**触发时机**：开始打开音视频解码器组件之前

#### FFP_MSG_PREPARED
```c
// read_thread() - 第 3334 行
ffp_notify_msg1(ffp, FFP_MSG_PREPARED);
```
**触发时机**：播放器准备完成，所有解码器已初始化

---

### 2.2 视频相关消息

#### FFP_MSG_VIDEO_SIZE_CHANGED
```c
// queue_picture() - 第 1629 行
ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, src_frame->width, src_frame->height);

// read_thread() - 第 3330 行（初始化时）
ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, codecpar->width, codecpar->height);
```
**触发时机**：
1. 视频帧入队时检测到尺寸变化
2. 视频流打开时通知初始尺寸

#### FFP_MSG_SAR_CHANGED
```c
// read_thread() - 第 3331 行
ffp_notify_msg3(ffp, FFP_MSG_SAR_CHANGED, codecpar->sample_aspect_ratio.num, codecpar->sample_aspect_ratio.den);
```
**触发时机**：视频流打开时通知像素宽高比

#### FFP_MSG_VIDEO_ROTATION_CHANGED
```c
// ffplay_video_thread() - 第 2204 行
ffp_notify_msg2(ffp, FFP_MSG_VIDEO_ROTATION_CHANGED, ffp_get_video_rotate_degrees(ffp));

// ffpipenode_android_mediacodec_vdec.c - 第 263/266 行
ffp_notify_msg2(ffp, FFP_MSG_VIDEO_ROTATION_CHANGED, rotate_degrees);
```
**触发时机**：视频线程启动时获取并通知视频旋转角度

#### FFP_MSG_VIDEO_DECODED_START
```c
// queue_picture() - 第 1680 行
ffp_notify_msg1(ffp, FFP_MSG_VIDEO_DECODED_START);
```
**触发时机**：首个视频帧解码完成并入队

#### FFP_MSG_VIDEO_RENDERING_START
```c
// video_image_display2() - 第 905/915 行
ffp_notify_msg1(ffp, FFP_MSG_VIDEO_RENDERING_START);
```
**触发时机**：首个视频帧渲染到屏幕

#### FFP_MSG_VIDEO_SEEK_RENDERING_START
```c
// video_image_display2() - 第 923/925 行
ffp_notify_msg2(ffp, FFP_MSG_VIDEO_SEEK_RENDERING_START, is_accurate ? 1 : 0);
```
**触发时机**：seek 后首个视频帧渲染完成

---

### 2.3 音频相关消息

#### FFP_MSG_AUDIO_DECODED_START
```c
// audio_thread() - 第 2623 行
ffp_notify_msg1(ffp, FFP_MSG_AUDIO_DECODED_START);
```
**触发时机**：首个音频帧解码完成

#### FFP_MSG_AUDIO_RENDERING_START
```c
// sdl_audio_callback() - 第 2702 行
ffp_notify_msg1(ffp, FFP_MSG_AUDIO_RENDERING_START);
```
**触发时机**：音频回调函数首次输出音频数据

#### FFP_MSG_AUDIO_SEEK_RENDERING_START
```c
// sdl_audio_callback() - 第 2709/2711 行
ffp_notify_msg2(ffp, FFP_MSG_AUDIO_SEEK_RENDERING_START, is_accurate ? 1 : 0);
```
**触发时机**：seek 后首个音频帧渲染完成

---

### 2.4 缓冲相关消息

#### FFP_MSG_BUFFERING_START
```c
// ffp_check_buffering_l() - 第 4590/4592 行
ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_START, is_first ? 1 : 0);
```
**触发时机**：检测到缓冲不足，开始缓冲（播放可能暂停）

#### FFP_MSG_BUFFERING_END
```c
// ffp_check_buffering_l() - 第 4600/4602 行
ffp_notify_msg2(ffp, FFP_MSG_BUFFERING_END, is_first ? 1 : 0);
```
**触发时机**：缓冲充足，恢复播放

#### FFP_MSG_BUFFERING_UPDATE
```c
// read_thread() - 第 3379 行
ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_UPDATE, 0, 0);

// ffp_statistic_l() - 第 4742 行
ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_UPDATE, (int)buf_time_position, buf_percent);
```
**触发时机**：缓冲进度定期更新

#### FFP_MSG_BUFFERING_BYTES_UPDATE
```c
// ffp_statistic_l() - 第 4719 行
ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_BYTES_UPDATE, cached_size, hwm_in_bytes);
```
**触发时机**：缓冲字节数更新（JNI层不处理）

#### FFP_MSG_BUFFERING_TIME_UPDATE
```c
// ffp_statistic_l() - 第 4707 行
ffp_notify_msg3(ffp, FFP_MSG_BUFFERING_TIME_UPDATE, cached_duration_in_ms, hwm_in_ms);
```
**触发时机**：缓冲时长更新（JNI层不处理）

---

### 2.5 Seek 相关消息

#### FFP_MSG_SEEK_COMPLETE
```c
// read_thread() - 第 3448 行
ffp_notify_msg3(ffp, FFP_MSG_SEEK_COMPLETE, (int)fftime_to_milliseconds(seek_target), ret);
```
**触发时机**：seek 操作完成

#### FFP_MSG_ACCURATE_SEEK_COMPLETE
```c
// queue_picture() - 第 1573/1597/1599 行
ffp_notify_msg2(ffp, FFP_MSG_ACCURATE_SEEK_COMPLETE, (int)(pts * 1000));

// sdl_audio_callback() - 第 2062/2087 行
ffp_notify_msg2(ffp, FFP_MSG_ACCURATE_SEEK_COMPLETE, (int)(audio_clock * 1000));
```
**触发时机**：精确 seek 完成，音视频帧已定位到目标位置

---

### 2.6 播放状态消息

#### FFP_MSG_COMPLETED
```c
// read_thread() - 第 3512 行（读取线程结束）
ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);

// video_refresh() - 第 4415 行（视频队列播放完毕）
ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);
```
**触发时机**：
1. 读取线程到达文件末尾且所有包已消费
2. 视频刷新检测到所有帧已播放完毕

#### FFP_MSG_ERROR
```c
// read_thread() - 第 3509 行
ffp_notify_msg1(ffp, FFP_MSG_ERROR);

// read_thread() - 第 3635 行
ffp_notify_msg2(ffp, FFP_MSG_ERROR, last_error);
```
**触发时机**：
1. 读取数据时发生错误（非 EOF）
2. 准备阶段发生错误（打开文件失败、找不到流信息等）

#### FFP_MSG_PLAYBACK_STATE_CHANGED
```c
// ijkplayer.c - 第 114 行
ffp_notify_msg1(mp->ffplayer, FFP_MSG_PLAYBACK_STATE_CHANGED);
```
**触发时机**：IjkMediaPlayer 状态机状态变化（JNI层不处理）

---

### 2.7 其他消息

#### FFP_MSG_TIMED_TEXT
```c
// video_image_display2() - 第 895 行
ffp_notify_msg4(ffp, FFP_MSG_TIMED_TEXT, 0, 0, buffered_text, sizeof(buffered_text));

// video_refresh_subtitle() - 第 1393 行（清空字幕）
ffp_notify_msg4(ffp, FFP_MSG_TIMED_TEXT, 0, 0, "", 1);
```
**触发时机**：字幕内容更新或需要清除字幕

#### FFP_MSG_GET_IMG_STATE
```c
// ffp_set_frame_at_time() - 第 549/551 行
ffp_notify_msg4(ffp, FFP_MSG_GET_IMG_STATE, (int)src_frame_pts, result, file_name, file_name_length);

// ffp_get_img() - 第 2252 行
ffp_notify_msg3(ffp, FFP_MSG_GET_IMG_STATE, 0, ret);

// ffp_get_frame_at_time() - 第 4102/4125 行
ffp_notify_msg3(ffp, FFP_MSG_GET_IMG_STATE, 0, -1);
```
**触发时机**：截图操作完成，通知截图结果

---

## 三、JNI 层消息映射 (ijkplayer_jni.c)

`message_loop_n()` 将 FFP_MSG_* 消息转换为 Android MediaPlayer 兼容的 MEDIA_* 事件：

| FFP 消息 | MEDIA 事件 | 参数映射 |
|----------|------------|----------|
| `FFP_MSG_FLUSH` | `MEDIA_NOP` | 0, 0 |
| `FFP_MSG_ERROR` | `MEDIA_ERROR` | MEDIA_ERROR_IJK_PLAYER, arg1 |
| `FFP_MSG_PREPARED` | `MEDIA_PREPARED` | 0, 0 |
| `FFP_MSG_COMPLETED` | `MEDIA_PLAYBACK_COMPLETE` | 0, 0 |
| `FFP_MSG_VIDEO_SIZE_CHANGED` | `MEDIA_SET_VIDEO_SIZE` | width, height |
| `FFP_MSG_SAR_CHANGED` | `MEDIA_SET_VIDEO_SAR` | num, den |
| `FFP_MSG_VIDEO_RENDERING_START` | `MEDIA_INFO` | MEDIA_INFO_VIDEO_RENDERING_START, 0 |
| `FFP_MSG_AUDIO_RENDERING_START` | `MEDIA_INFO` | MEDIA_INFO_AUDIO_RENDERING_START, 0 |
| `FFP_MSG_VIDEO_ROTATION_CHANGED` | `MEDIA_INFO` | MEDIA_INFO_VIDEO_ROTATION_CHANGED, degree |
| `FFP_MSG_AUDIO_DECODED_START` | `MEDIA_INFO` | MEDIA_INFO_AUDIO_DECODED_START, 0 |
| `FFP_MSG_VIDEO_DECODED_START` | `MEDIA_INFO` | MEDIA_INFO_VIDEO_DECODED_START, 0 |
| `FFP_MSG_OPEN_INPUT` | `MEDIA_INFO` | MEDIA_INFO_OPEN_INPUT, 0 |
| `FFP_MSG_FIND_STREAM_INFO` | `MEDIA_INFO` | MEDIA_INFO_FIND_STREAM_INFO, 0 |
| `FFP_MSG_COMPONENT_OPEN` | `MEDIA_INFO` | MEDIA_INFO_COMPONENT_OPEN, 0 |
| `FFP_MSG_BUFFERING_START` | `MEDIA_INFO` | MEDIA_INFO_BUFFERING_START, arg1 |
| `FFP_MSG_BUFFERING_END` | `MEDIA_INFO` | MEDIA_INFO_BUFFERING_END, arg1 |
| `FFP_MSG_BUFFERING_UPDATE` | `MEDIA_BUFFERING_UPDATE` | position, percent |
| `FFP_MSG_BUFFERING_BYTES_UPDATE` | (不处理) | - |
| `FFP_MSG_BUFFERING_TIME_UPDATE` | (不处理) | - |
| `FFP_MSG_SEEK_COMPLETE` | `MEDIA_SEEK_COMPLETE` | 0, 0 |
| `FFP_MSG_ACCURATE_SEEK_COMPLETE` | `MEDIA_INFO` | MEDIA_INFO_MEDIA_ACCURATE_SEEK_COMPLETE, position |
| `FFP_MSG_PLAYBACK_STATE_CHANGED` | (不处理) | - |
| `FFP_MSG_TIMED_TEXT` | `MEDIA_TIMED_TEXT` | 0, 0, text |
| `FFP_MSG_GET_IMG_STATE` | `MEDIA_GET_IMG_STATE` | timestamp, result, filename |
| `FFP_MSG_VIDEO_SEEK_RENDERING_START` | `MEDIA_INFO` | MEDIA_INFO_VIDEO_SEEK_RENDERING_START, arg1 |
| `FFP_MSG_AUDIO_SEEK_RENDERING_START` | `MEDIA_INFO` | MEDIA_INFO_AUDIO_SEEK_RENDERING_START, arg1 |

---

## 四、消息处理流程

### 4.1 内部请求消息 (FFP_REQ_*) 处理流程

```
UI线程调用 ijkmp_start()/ijkmp_pause()/ijkmp_seek_to()
    │
    ▼
ijkmp_start_l() / ijkmp_pause_l() / ijkmp_seek_to_l()
    │
    ├─ 移除队列中同类型的旧消息（防抖）
    │
    ▼
ffp_notify_msg(FFP_REQ_START/PAUSE/SEEK)
    │
    ▼
msg_queue_put() → 消息入队
    │
    ▼
ijkmp_get_msg() 取出消息
    │
    ├─ 执行 ffp_start_l() / ffp_pause_l() / ffp_seek_to_l()
    ├─ 更新 IjkMediaPlayer 状态机
    │
    ▼
continue; (不返回给 message_loop_n)
```

### 4.2 外部通知消息 (FFP_MSG_*) 处理流程

```
FFPlayer/ffplay 检测到事件
    │
    ▼
ffp_notify_msg*(FFP_MSG_*)
    │
    ▼
msg_queue_put() → 消息入队
    │
    ▼
ijkmp_get_msg() 取出消息
    │
    ├─ 部分消息更新 IjkMediaPlayer 状态机
    │   (FFP_MSG_PREPARED → MP_STATE_PREPARED)
    │   (FFP_MSG_COMPLETED → MP_STATE_COMPLETED)
    │
    ▼
返回消息给 message_loop_n()
    │
    ▼
转换为 MEDIA_* 事件
    │
    ▼
post_event() → JNI调用
    │
    ▼
Java层 IjkMediaPlayer.postEventFromNative()
    │
    ▼
EventHandler 分发给注册的 Listener
```

---

## 五、状态机与消息的关系

### 5.1 IjkMediaPlayer 状态机

```
                           ┌──────────────────┐
                           │   MP_STATE_IDLE  │ (初始状态)
                           └────────┬─────────┘
                                    │ setDataSource()
                                    ▼
                    ┌───────────────────────────────┐
                    │    MP_STATE_INITIALIZED       │
                    └───────────────┬───────────────┘
                                    │ prepareAsync()
                                    ▼
                    ┌───────────────────────────────┐
                    │  MP_STATE_ASYNC_PREPARING     │
                    └───────────────┬───────────────┘
                                    │ FFP_MSG_PREPARED
                                    ▼
        ┌───────────────────────────────────────────────────┐
        │               MP_STATE_PREPARED                    │
        └──────────┬───────────────────────────┬────────────┘
                   │ start()                    │ stop()
                   ▼                            ▼
    ┌───────────────────────┐       ┌─────────────────────┐
    │   MP_STATE_STARTED    │◄─────►│  MP_STATE_STOPPED   │
    └───────────┬───────────┘       └─────────────────────┘
                │ pause()                       
                ▼                    
    ┌───────────────────────┐       
    │   MP_STATE_PAUSED     │       
    └───────────────────────┘       
                │ FFP_MSG_COMPLETED
                ▼
    ┌───────────────────────┐
    │  MP_STATE_COMPLETED   │
    └───────────────────────┘
```

### 5.2 更新状态机的消息

在 `ijkmp_get_msg()` 中处理：

| 消息 | 状态变化 |
|------|----------|
| `FFP_MSG_PREPARED` | → `MP_STATE_PREPARED` |
| `FFP_MSG_COMPLETED` | → `MP_STATE_COMPLETED` |
| `FFP_MSG_SEEK_COMPLETE` | → `MP_STATE_STARTED` (如果之前是 STARTED) |
| `FFP_REQ_START` | → `MP_STATE_STARTED` |
| `FFP_REQ_PAUSE` | → `MP_STATE_PAUSED` |

**注意**：`FFP_MSG_ERROR` 应该将状态更新为 `MP_STATE_ERROR`，但当前实现中未在 `ijkmp_get_msg()` 中处理，这是一个设计上的疏漏。

---

## 六、关键代码位置

| 文件 | 功能 |
|------|------|
| `ff_ffmsg.h` | 消息类型定义 |
| `ff_ffmsg_queue.h` | 消息队列实现 |
| `ff_ffplay.c` | 消息生产者（FFPlayer 核心逻辑） |
| `ijkplayer.c` | 消息过滤与状态机管理 |
| `ijkplayer_jni.c` | JNI 层消息处理与转发 |

---

## 七、实际应用场景

### 7.1 播放进度跟踪
监听 `FFP_MSG_BUFFERING_UPDATE` 获取缓冲进度。

### 7.2 首帧渲染统计
监听 `FFP_MSG_VIDEO_RENDERING_START` 计算首帧延迟。

### 7.3 错误处理
监听 `FFP_MSG_ERROR` 处理播放异常。

### 7.4 播放完成处理
监听 `FFP_MSG_COMPLETED` 进行下一个视频播放或 UI 更新。

### 7.5 视频尺寸适配
监听 `FFP_MSG_VIDEO_SIZE_CHANGED` 和 `FFP_MSG_VIDEO_ROTATION_CHANGED` 调整播放视图。

### 7.6 字幕显示
监听 `FFP_MSG_TIMED_TEXT` 更新字幕 UI。

---

## 八、总结

ijkplayer 的消息系统设计清晰：

1. **生产者**：`ff_ffplay.c` 中的各个线程（read_thread、video_thread、audio_thread）
2. **队列**：`ffplayer->msg_queue`（线程安全的消息队列）
3. **消费者**：
   - `ijkmp_get_msg()`：过滤内部请求，更新状态机
   - `message_loop_n()`：处理外部通知，转发到 Java 层

这种设计实现了：
- 播放层与 UI 层的解耦
- 异步非阻塞的事件通知
- 跨平台的统一接口
- 与 Android MediaPlayer API 的兼容

