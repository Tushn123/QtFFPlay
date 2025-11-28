# ijkplayer 如何拆解 ffplay.c - 深度分析

## 概述

FFmpeg 的 `ffplay.c` 是一个**单体文件**（~3500 行），包含了完整的播放器实现。ijkplayer 将其重构为**模块化架构**，拆分成多个文件，并增加了抽象层，使其可以跨平台使用。

---

## 一、原始 ffplay.c 的结构

### FFmpeg ffplay.c（单体架构）

```
ffplay.c (约 3500 行)
├── 数据结构定义
│   ├── VideoState      // 播放器核心状态
│   ├── PacketQueue     // 数据包队列
│   ├── FrameQueue      // 帧队列
│   ├── Decoder         // 解码器
│   └── Clock           // 时钟同步
├── 全局变量
│   ├── static VideoState *cur_stream
│   ├── static AVPacket flush_pkt
│   └── 各种全局选项
├── 队列操作函数
│   ├── packet_queue_init()
│   ├── packet_queue_put()
│   └── packet_queue_get()
├── 解码线程
│   ├── read_thread()       // 读取数据包
│   ├── audio_thread()      // 音频解码
│   ├── video_thread()      // 视频解码
│   └── subtitle_thread()   // 字幕解码
├── 音视频同步
│   ├── get_master_sync_type()
│   ├── get_master_clock()
│   └── synchronize_audio()
├── 渲染输出
│   ├── video_refresh()     // 视频刷新
│   ├── video_display()     // 视频显示
│   └── SDL 音频回调
├── 控制逻辑
│   ├── stream_open()
│   ├── stream_close()
│   ├── stream_seek()
│   └── toggle_pause()
└── main() 函数
```

**问题**：
- ❌ 单文件 3500+ 行，难以维护
- ❌ 全局变量多，不支持多实例
- ❌ SDL 强耦合，无法跨平台
- ❌ 硬编码 SDL 输出，无法定制

---

## 二、ijkplayer 的拆解策略

### 拆解原则

ijkplayer 采用了以下拆解原则：

1. **数据结构分离** - 将所有结构定义提取到 `.h` 文件
2. **全局变量转为实例变量** - 用 `FFPlayer` 结构体封装
3. **功能模块化** - 按职责拆分成多个文件
4. **平台抽象** - 引入 SDL 抽象层和 Pipeline 模式
5. **保留核心逻辑** - 播放逻辑 90% 保持不变

---

## 三、拆解后的文件结构

### ijkplayer 的模块化架构

```
ijkmedia/ijkplayer/
│
├── 1️⃣ 数据结构定义层
│   ├── ff_ffplay_def.h         ✨ 核心数据结构（从 ffplay.c 提取）
│   │   ├── PacketQueue         // 数据包队列
│   │   ├── FrameQueue          // 帧队列
│   │   ├── Decoder             // 解码器
│   │   ├── Clock               // 时钟
│   │   ├── VideoState          // 播放状态（对应原 ffplay 的核心）
│   │   ├── FFPlayer            // 🆕 新增：封装所有全局变量
│   │   ├── FFStatistic         // 🆕 新增：统计信息
│   │   └── FFDemuxCacheControl // 🆕 新增：缓冲控制
│
├── 2️⃣ 核心播放逻辑层
│   ├── ff_ffplay.h/c           ✨ 主要播放逻辑（对应原 ffplay.c）
│   │   ├── read_thread()       // 读取线程
│   │   ├── audio_thread()      // 音频解码线程
│   │   ├── video_thread()      // 视频解码线程
│   │   ├── video_refresh()     // 视频刷新
│   │   └── 音视频同步逻辑
│
├── 3️⃣ 辅助功能层
│   ├── ff_cmdutils.h/c         ✨ FFmpeg 命令行工具
│   ├── ff_fferror.h            ✨ 错误码定义
│   ├── ff_ffmsg.h              ✨ 消息定义
│   ├── ff_ffmsg_queue.h        ✨ 消息队列
│   └── ff_ffplay_options.h     ✨ 播放器选项
│
├── 4️⃣ 平台抽象层（🆕 新增）
│   ├── ff_ffpipeline.h/c       // 解码管道抽象
│   ├── ff_ffpipenode.h/c       // 管道节点抽象
│   └── pipeline/
│       ├── ffpipeline_ffplay.c      // 软解管道实现
│       └── ffpipenode_ffplay_vdec.c // 软解节点实现
│
├── 5️⃣ 自定义 IO 层（🆕 新增）
│   ├── ijkavformat/
│   │   ├── ijkio*.c            // 异步 IO、缓存 IO
│   │   ├── ijkurlhook.c        // URL 钩子
│   │   └── ijklivehook.c       // 直播钩子
│
├── 6️⃣ 元数据层（🆕 新增）
│   ├── ijkmeta.h/c             // 媒体元数据
│
└── 7️⃣ 外部接口层（🆕 新增）
    ├── ijkplayer.h/c           // 播放器 API
    └── android/
        └── ijkplayer_android.c // Android JNI 绑定
```

---

## 四、关键拆解细节

### 4.1 数据结构提取：ffplay.c → ff_ffplay_def.h

#### **原始 ffplay.c（内嵌定义）**

```c
// ffplay.c 中直接定义
typedef struct VideoState {
    SDL_Thread *read_tid;
    AVFormatContext *ic;
    Clock audclk;
    Clock vidclk;
    FrameQueue pictq;
    Decoder auddec;
    Decoder viddec;
    // ... 100+ 个字段
} VideoState;

// 全局变量
static VideoState *cur_stream;
static AVPacket flush_pkt;
static int audio_disable;
static int video_disable;
// ... 30+ 个全局变量
```

#### **ijkplayer 拆解后（ff_ffplay_def.h）**

```c
// ff_ffplay_def.h - 结构定义
typedef struct VideoState {
    SDL_Thread *read_tid;
    SDL_Thread _read_tid;  // 🆕 内嵌实例，避免动态分配
    AVInputFormat *iformat;
    int abort_request;
    int force_refresh;
    // ... 保持原有字段
    
    /* extra fields - 🆕 新增扩展字段 */
    SDL_mutex  *play_mutex;
    SDL_Thread *video_refresh_tid;
    int buffering_on;
    int pause_req;
    int dropping_frame;
    // ... 更多扩展
} VideoState;

// 🆕 新增：FFPlayer 封装所有全局变量
typedef struct FFPlayer {
    const AVClass *av_class;
    
    /* ffplay context */
    VideoState *is;  // 原 ffplay 的 cur_stream
    
    /* format/codec options */
    AVDictionary *format_opts;
    AVDictionary *codec_opts;
    AVDictionary *sws_dict;
    
    /* ffplay options specified by the user */
    char *input_filename;       // 原全局变量 input_filename
    int audio_disable;          // 原全局变量 audio_disable
    int video_disable;          // 原全局变量 video_disable
    int av_sync_type;           // 原全局变量 av_sync_type
    // ... 所有原全局变量变成实例变量
    
    /* extra fields - 🆕 平台扩展 */
    SDL_Aout *aout;             // 音频输出抽象
    SDL_Vout *vout;             // 视频输出抽象
    struct IJKFF_Pipeline *pipeline;  // 解码管道
    MessageQueue msg_queue;     // 消息队列
    int mediacodec_all_videos;  // Android 硬解
    int videotoolbox;           // iOS 硬解
    // ... 更多平台特性
} FFPlayer;
```

**关键改进**：
- ✅ **全局变量 → 实例变量**：支持多实例播放器
- ✅ **扩展字段分离**：通过注释标记原有/新增字段
- ✅ **平台扩展**：`FFPlayer` 包含平台特定字段

---

### 4.2 播放逻辑提取：ffplay.c → ff_ffplay.c

#### **代码映射关系**

| 原 ffplay.c | ijkplayer ff_ffplay.c | 说明 |
|-------------|----------------------|------|
| `static VideoState *cur_stream` | `FFPlayer *ffp` | 实例化 |
| `stream_open()` | `ffp_prepare_async_l()` | 重命名 |
| `read_thread()` | `read_thread()` | **保持不变** |
| `audio_thread()` | `audio_thread()` | **保持不变** |
| `video_thread()` | `video_thread()` | **保持不变** |
| `video_refresh()` | `video_refresh_thread()` | 独立线程 |
| `toggle_pause()` | `ffp_toggle_pause()` | 添加 ffp 前缀 |
| SDL 音频回调 | `sdl_audio_callback()` | **保持不变** |

#### **核心函数对比**

**原 ffplay.c（全局访问）**：
```c
static int read_thread(void *arg) {
    VideoState *is = arg;
    AVFormatContext *ic = NULL;
    
    // 访问全局变量
    if (audio_disable)
        is->audio_stream = -1;
    
    ic = avformat_alloc_context();
    // ...
}
```

**ijkplayer ff_ffplay.c（实例访问）**：
```c
static int read_thread(void *arg) {
    FFPlayer *ffp = arg;          // 🔄 改为 FFPlayer
    VideoState *is = ffp->is;     // 🔄 从 ffp 获取
    AVFormatContext *ic = NULL;
    
    // 🔄 访问实例变量
    if (ffp->audio_disable)       // ffp-> 而非全局变量
        is->audio_stream = -1;
    
    ic = avformat_alloc_context();
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = is;
    // ...
}
```

**改动说明**：
- 🔄 `VideoState *is` → `FFPlayer *ffp` + `VideoState *is = ffp->is`
- 🔄 所有全局变量访问改为 `ffp->xxx`
- ✅ 核心逻辑（FFmpeg API 调用）**100% 保持不变**

---

### 4.3 SDL 抽象化：SDL → ijksdl

#### **原 ffplay.c（硬编码 SDL）**

```c
// ffplay.c 直接使用 SDL
#include <SDL.h>

SDL_AudioSpec wanted_spec, spec;
SDL_OpenAudio(&wanted_spec, &spec);

SDL_Surface *screen;
screen = SDL_SetVideoMode(w, h, 0, flags);
SDL_DisplayYUVOverlay(vp->bmp, &rect);
```

#### **ijkplayer（抽象化 SDL）**

```c
// ff_ffplay.c 使用抽象接口
#include "ijksdl/ijksdl_aout.h"
#include "ijksdl/ijksdl_vout.h"

// 音频输出抽象
SDL_Aout *aout = ffp->aout;
SDL_AoutOpenAudio(aout, &desired, &obtained);
SDL_AoutPauseAudio(aout, pause_on);

// 视频输出抽象
SDL_Vout *vout = ffp->vout;
SDL_VoutOverlay *overlay = SDL_Vout_CreateOverlay(w, h, format, vout);
SDL_VoutDisplayYUVOverlay(vout, overlay);
```

**抽象层定义（ijksdl_aout.h）**：
```c
typedef struct SDL_Aout SDL_Aout;
struct SDL_Aout {
    SDL_Aout_Opaque *opaque;  // 平台私有数据
    
    // 函数指针 - 平台实现
    int  (*open_audio)(SDL_Aout *aout, ...);
    void (*pause_audio)(SDL_Aout *aout, int pause_on);
    void (*flush_audio)(SDL_Aout *aout);
    void (*close_audio)(SDL_Aout *aout);
    // ...
};
```

**平台实现（Android）**：
```c
// ijksdl/android/ijksdl_aout_android_audiotrack.c
static int aout_open_audio(SDL_Aout *aout, ...) {
    // 使用 Android AudioTrack
    SDL_Android_AudioTrack_Opaque *opaque = aout->opaque;
    opaque->audio_track = AudioTrack_new(...);
    return 0;
}

SDL_Aout* SDL_AoutAndroid_CreateForAudioTrack() {
    SDL_Aout *aout = calloc(1, sizeof(SDL_Aout));
    aout->open_audio = aout_open_audio;
    aout->pause_audio = aout_pause_audio;
    // ...
    return aout;
}
```

**关键改进**：
- ✅ 解耦平台依赖：`ff_ffplay.c` 不知道 Android/iOS
- ✅ 可替换实现：软件渲染、硬件渲染、空渲染
- ✅ 便于测试：可以注入 dummy 实现

---

### 4.4 Pipeline 模式：硬件解码抽象

#### **原 ffplay.c（软解固定）**

```c
// ffplay.c 只支持软件解码
AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
avcodec_open2(avctx, codec, &opts);
```

#### **ijkplayer（Pipeline 抽象）**

```c
// ff_ffpipeline.h - 管道接口
typedef struct IJKFF_Pipeline {
    IJKFF_Pipenode *(*func_open_video_decoder)(IJKFF_Pipeline *pipeline, FFPlayer *ffp);
    SDL_Aout       *(*func_open_audio_output)(IJKFF_Pipeline *pipeline, FFPlayer *ffp);
} IJKFF_Pipeline;

// ff_ffpipenode.h - 解码节点接口
typedef struct IJKFF_Pipenode {
    int (*func_run_sync)(IJKFF_Pipenode *node);
    int (*func_flush)(IJKFF_Pipenode *node);
    // ...
} IJKFF_Pipenode;
```

**软解实现（ffpipeline_ffplay.c）**：
```c
static IJKFF_Pipenode *func_open_video_decoder(IJKFF_Pipeline *pipeline, FFPlayer *ffp) {
    // 返回 FFmpeg 软解节点
    return ffpipenode_create_video_decoder_from_ffplay(ffp);
}

IJKFF_Pipeline *ffpipeline_create_from_ffplay(FFPlayer *ffp) {
    IJKFF_Pipeline *pipeline = ffpipeline_alloc(...);
    pipeline->func_open_video_decoder = func_open_video_decoder;
    return pipeline;
}
```

**硬解实现（Android MediaCodec）**：
```c
// android/pipeline/ffpipeline_android.c
static IJKFF_Pipenode *func_open_video_decoder(IJKFF_Pipeline *pipeline, FFPlayer *ffp) {
    if (ffp->mediacodec_avc && is_avc) {
        // 返回 MediaCodec 硬解节点
        return ffpipenode_create_video_decoder_from_android_mediacodec(...);
    }
    // 降级到软解
    return ffpipenode_create_video_decoder_from_ffplay(ffp);
}
```

**关键改进**：
- ✅ 策略模式：运行时选择软解/硬解
- ✅ 平台扩展：Android MediaCodec、iOS VideoToolbox
- ✅ 降级机制：硬解失败自动切换软解

---

### 4.5 消息队列：异步通信

#### **原 ffplay.c（同步事件）**

```c
// ffplay.c 使用 SDL 事件
SDL_Event event;
event.type = FF_REFRESH_EVENT;
SDL_PushEvent(&event);

// 主循环处理
SDL_WaitEvent(&event);
switch (event.type) {
    case FF_REFRESH_EVENT:
        video_refresh(cur_stream);
        break;
}
```

#### **ijkplayer（消息队列）**

```c
// ff_ffmsg.h - 消息定义
#define FFP_MSG_PREPARED        1
#define FFP_MSG_COMPLETED       2
#define FFP_MSG_VIDEO_SIZE_CHANGED  3
#define FFP_MSG_ERROR           100

typedef struct AVMessage {
    int what;
    int arg1;
    int arg2;
    void *obj;
} AVMessage;

// ff_ffmsg_queue.h - 消息队列（宏定义）
#define AVMessage_queue MessageQueue
#define msg_queue_put(q, msg)    ...
#define msg_queue_get(q, msg, block) ...
```

**发送消息（ff_ffplay.c）**：
```c
// 播放准备完成
ffp_notify_msg1(ffp, FFP_MSG_PREPARED);

// 视频尺寸变化
ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, width, height);

// 播放完成
ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);
```

**接收消息（ijkplayer.c）**：
```c
int ijkmp_get_msg(IjkMediaPlayer *mp, AVMessage *msg, int block) {
    int retval = msg_queue_get(&mp->ffplayer->msg_queue, msg, block);
    
    switch (msg->what) {
    case FFP_MSG_PREPARED:
        ijkmp_change_state_l(mp, MP_STATE_PREPARED);
        break;
    case FFP_MSG_COMPLETED:
        ijkmp_change_state_l(mp, MP_STATE_COMPLETED);
        break;
    }
    return retval;
}
```

**关键改进**：
- ✅ 解耦通信：播放器内核与外部接口分离
- ✅ 跨线程安全：消息队列保证线程安全
- ✅ 异步架构：支持 Android 主线程更新 UI

---

## 五、拆解对比总结

### 文件数量对比

| 项目 | FFmpeg ffplay | ijkplayer |
|------|---------------|-----------|
| **文件数** | 1 个文件 | 20+ 个文件 |
| **代码行数** | ~3500 行 | 核心 ~5000 行 + 平台 ~3000 行 |
| **数据结构定义** | 内嵌在 .c | 独立 .h 文件 |
| **全局变量** | 30+ 个 | 0 个（封装在 FFPlayer） |
| **平台支持** | Linux/macOS/Windows | Android/iOS + 可扩展 |
| **硬件解码** | 不支持 | 支持（Pipeline 模式） |
| **多实例** | ❌ 不支持 | ✅ 支持 |

---

### 架构对比图

#### **原始 ffplay.c（单体）**

```
┌───────────────────────────────────┐
│         ffplay.c (3500行)         │
│                                   │
│  ┌─────────────────────────────┐ │
│  │ 数据结构 (内嵌定义)         │ │
│  ├─────────────────────────────┤ │
│  │ 全局变量 (30+ 个)           │ │
│  ├─────────────────────────────┤ │
│  │ 队列操作函数                │ │
│  ├─────────────────────────────┤ │
│  │ 解码线程 (read/audio/video) │ │
│  ├─────────────────────────────┤ │
│  │ 音视频同步                  │ │
│  ├─────────────────────────────┤ │
│  │ SDL 渲染 (硬编码)           │ │
│  ├─────────────────────────────┤ │
│  │ 控制逻辑                    │ │
│  ├─────────────────────────────┤ │
│  │ main() 函数                 │ │
│  └─────────────────────────────┘ │
└───────────────────────────────────┘
         ↓ 直接调用
┌───────────────────────────────────┐
│          SDL 库 (Linux)           │
└───────────────────────────────────┘
```

#### **ijkplayer（模块化）**

```
┌─────────────────────────────────────────────────┐
│          ijkplayer.h/c (API 层)                 │
│  - 播放器外部接口                                │
│  - 状态机管理                                   │
└─────────────────────────────────────────────────┘
                    ↓ 调用
┌─────────────────────────────────────────────────┐
│        ff_ffplay.h/c (核心逻辑层)               │
│  - read_thread()                                │
│  - audio_thread() / video_thread()              │
│  - 音视频同步                                   │
│  - 依赖：FFPlayer 实例（封装全局变量）          │
└─────────────────────────────────────────────────┘
         ↓ 数据结构              ↓ 平台抽象
┌─────────────────────┐   ┌───────────────────────┐
│  ff_ffplay_def.h    │   │   SDL 抽象层          │
│  - PacketQueue      │   │  - ijksdl_vout.h      │
│  - FrameQueue       │   │  - ijksdl_aout.h      │
│  - Decoder          │   │  - ijksdl_thread.h    │
│  - VideoState       │   └───────────────────────┘
│  - FFPlayer         │              ↓ 实现
└─────────────────────┘   ┌───────────────────────┐
         ↓ 使用            │   平台实现层          │
┌─────────────────────┐   │  Android:             │
│  Pipeline 抽象层    │   │  - AudioTrack         │
│  - ff_ffpipeline.h  │   │  - NativeWindow       │
│  - ff_ffpipenode.h  │   │  - MediaCodec         │
└─────────────────────┘   │  iOS:                 │
         ↓ 实现            │  - AudioQueue         │
┌─────────────────────┐   │  - OpenGL ES          │
│  软解/硬解实现      │   │  - VideoToolbox       │
│  - ffpipeline_      │   └───────────────────────┘
│    ffplay.c (软解)  │
│  - ffpipeline_      │
│    android.c (硬解) │
└─────────────────────┘
```

---

## 六、拆解的关键设计模式

### 6.1 门面模式（Facade Pattern）

**ijkplayer.h/c** 作为门面，隐藏 `ff_ffplay.c` 的复杂性：

```c
// 外部调用（简单）
IjkMediaPlayer *mp = ijkmp_create(msg_loop);
ijkmp_set_data_source(mp, "video.mp4");
ijkmp_prepare_async(mp);
ijkmp_start(mp);

// 内部实现（复杂）
ijkmp_start() {
    → ffp_start_l(mp->ffplayer) 
      → ffp_notify_msg1(ffp, FFP_REQ_START)
        → msg_queue_put()
          → read_thread 处理
            → 启动解码线程
}
```

### 6.2 策略模式（Strategy Pattern）

**Pipeline** 运行时选择解码策略：

```c
// 创建管道（运行时决定）
if (ffp->mediacodec_avc) {
    ffp->pipeline = ffpipeline_create_from_android(ffp);  // 硬解
} else {
    ffp->pipeline = ffpipeline_create_from_ffplay(ffp);   // 软解
}

// 打开解码器（多态）
IJKFF_Pipenode *node = ffpipeline_open_video_decoder(ffp->pipeline, ffp);
```

### 6.3 观察者模式（Observer Pattern）

**消息队列** 实现解耦通信：

```c
// 发布者（ff_ffplay.c）
ffp_notify_msg1(ffp, FFP_MSG_PREPARED);

// 订阅者（ijkplayer.c）
AVMessage msg;
ijkmp_get_msg(mp, &msg, 1);
if (msg.what == FFP_MSG_PREPARED) {
    // 通知 Android/iOS 层
}
```

### 6.4 模板方法模式（Template Method Pattern）

**SDL 抽象接口** 定义算法骨架，子类实现细节：

```c
// 模板方法（ijksdl_aout.c）
int SDL_AoutOpenAudio(SDL_Aout *aout, ...) {
    // 前处理
    // 调用子类实现
    return aout->open_audio(aout, desired, obtained);
    // 后处理
}

// 子类实现（Android）
aout->open_audio = aout_open_audio_android_audiotrack;

// 子类实现（iOS）
aout->open_audio = aout_open_audio_ios_audioqueue;
```

---

## 七、拆解的优势

### ✅ 可维护性

| 方面 | ffplay.c | ijkplayer |
|------|----------|-----------|
| **单文件长度** | 3500 行 | < 1000 行/文件 |
| **职责分离** | 混在一起 | 清晰分层 |
| **修改影响** | 牵一发动全身 | 局部修改 |

### ✅ 可测试性

```c
// ijkplayer 可以单独测试每个模块
void test_packet_queue() {
    PacketQueue q;
    packet_queue_init(&q);
    // 单元测试...
}

void test_dummy_audio() {
    SDL_Aout *aout = SDL_AoutDummy_Create();  // 注入假实现
    // 不需要真实音频设备
}
```

### ✅ 可扩展性

```c
// 新增平台只需实现 SDL 接口
// Windows 实现
SDL_Aout* SDL_AoutWindows_Create() {
    SDL_Aout *aout = calloc(1, sizeof(SDL_Aout));
    aout->open_audio = aout_open_audio_windows;
    return aout;
}

// 新增硬解只需实现 Pipeline
IJKFF_Pipeline *ffpipeline_create_from_windows(FFPlayer *ffp) {
    pipeline->func_open_video_decoder = open_dxva2_decoder;
    return pipeline;
}
```

### ✅ 可复用性

```c
// ijkplayer 核心可用于不同项目
// Android 项目
IjkMediaPlayer *mp = ijkmp_create(...);

// iOS 项目（同样的 API）
IJKFFMoviePlayerController *player = [[IJKFFMoviePlayerController alloc] init];

// Qt Windows 项目（本次移植）
IJKMediaPlayer *player = new IJKMediaPlayer();
```

---

## 八、拆解的代价

### ❌ 复杂度增加

- 文件数从 1 个变成 20+ 个
- 需要理解抽象层和设计模式
- 调用链变长：`ijkmp_start()` → `ffp_start_l()` → `ffp_notify_msg1()` → ...

### ❌ 性能开销（微小）

- 函数指针调用 vs 直接调用
- 消息队列通信 vs 直接调用
- **实际影响**: 可忽略（< 1% CPU）

### ❌ 学习曲线

- 原 ffplay.c: 1 个文件，从头读到尾
- ijkplayer: 需要理解架构，知道从哪里开始

---

## 九、移植建议

基于这个拆解分析，移植到 Qt Windows 应该：

### ✅ 直接复用（不需改动）

```
✅ ff_ffplay.c/h           - 核心播放逻辑
✅ ff_ffplay_def.h         - 数据结构定义
✅ ff_ffpipeline.h/c       - Pipeline 抽象
✅ ff_cmdutils.h/c         - 工具函数
✅ ijkavformat/*           - 自定义 IO
✅ ijkavutil/*             - 工具库
✅ pipeline/ffpipeline_ffplay.c  - 软解实现
```

### 🔄 需要适配（小改）

```
🔄 ijkplayer.c             - 改日志宏（ALOGD → printf）
🔄 ff_ffplay.c             - 注释掉 Android 特定代码
🔄 config.h                - 添加 Windows 宏定义
```

### 🆕 需要新建（核心工作）

```
🆕 ijksdl/windows/ijksdl_vout_qt_opengl.cpp
🆕 ijksdl/windows/ijksdl_aout_qt_audio.cpp
🆕 ijksdl_thread.c (Windows 实现)
🆕 ijksdl_mutex.c (Windows 实现)
🆕 qt-wrapper/IJKMediaPlayer.cpp (Qt 封装)
```

### 📊 工作量分配

```
复用代码：70%（无需改动）
小幅修改：20%（改日志宏、条件编译）
新建代码：10%（SDL Windows 实现 + Qt 封装）
```

---

## 总结

ijkplayer 对 ffplay.c 的拆解是**教科书级的重构案例**：

1. **数据结构提取** → `ff_ffplay_def.h`
2. **全局变量封装** → `FFPlayer` 结构体
3. **平台抽象** → `ijksdl/*` 接口
4. **解码抽象** → `Pipeline` 模式
5. **消息解耦** → `MessageQueue`
6. **分层架构** → API 层 → 逻辑层 → 抽象层 → 平台层

核心播放逻辑保持 **90% 不变**，但通过抽象层实现了：
- ✅ 跨平台（Android/iOS/Windows/Linux）
- ✅ 多实例（一个进程多个播放器）
- ✅ 可扩展（硬件解码、自定义 IO）
- ✅ 可测试（依赖注入、模拟实现）

这就是为什么移植到 Qt Windows 只需 **3-4 周**，而不是从零开发 **3-6 个月**！

