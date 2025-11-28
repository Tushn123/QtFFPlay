# ijkplayer 项目架构分析

## 项目概述
ijkplayer 是 Bilibili 开源的基于 FFmpeg 的跨平台视频播放器，支持 Android 和 iOS。

---

## 核心架构分层

```
┌────────────────────────────────────────────────────┐
│                  应用层 (App Layer)                │
│  Android: Java/Kotlin    iOS: Objective-C/Swift   │
└────────────────────────────────────────────────────┘
                         ↓↑
┌────────────────────────────────────────────────────┐
│              平台绑定层 (Binding Layer)            │
│  Android: JNI            iOS: Obj-C Bridge         │
│  - ijkplayer_jni.c      - IJKMediaPlayer.m         │
└────────────────────────────────────────────────────┘
                         ↓↑
┌────────────────────────────────────────────────────┐
│          播放器API层 (Player API Layer)            │
│              ijkmedia/ijkplayer/                   │
│  - ijkplayer.h/c  (播放器主API)                    │
│  - 状态机管理                                      │
│  - 播放控制接口                                    │
└────────────────────────────────────────────────────┘
                         ↓↑
┌────────────────────────────────────────────────────┐
│          核心播放层 (Core Player Layer)            │
│              ijkmedia/ijkplayer/                   │
│  - ff_ffplay.c/h  (核心播放逻辑)                   │
│  - 解码线程管理                                    │
│  - 音视频同步                                      │
│  - 缓冲控制                                        │
└────────────────────────────────────────────────────┘
                         ↓↑
┌────────────────────────────────────────────────────┐
│          SDL抽象层 (SDL Abstraction Layer)         │
│              ijkmedia/ijksdl/                      │
│  ┌──────────────────┐  ┌──────────────────┐       │
│  │  视频输出抽象     │  │  音频输出抽象     │       │
│  │  ijksdl_vout.h   │  │  ijksdl_aout.h   │       │
│  └──────────────────┘  └──────────────────┘       │
│  ┌──────────────────┐  ┌──────────────────┐       │
│  │  线程封装         │  │  互斥锁封装       │       │
│  │  ijksdl_thread   │  │  ijksdl_mutex    │       │
│  └──────────────────┘  └──────────────────┘       │
└────────────────────────────────────────────────────┘
                         ↓↑
┌────────────────────────────────────────────────────┐
│       平台实现层 (Platform Implementation)          │
│  Android:                iOS:                      │
│  - AudioTrack         - AudioQueue                 │
│  - NativeWindow       - OpenGL ES                  │
│  - MediaCodec (硬解)   - VideoToolbox (硬解)        │
└────────────────────────────────────────────────────┘
                         ↓↑
┌────────────────────────────────────────────────────┐
│                FFmpeg 库                           │
│  - libavcodec   (解码)                             │
│  - libavformat  (解封装)                           │
│  - libswscale   (图像缩放/格式转换)                │
│  - libswresample (音频重采样)                      │
│  - libavutil    (工具函数)                         │
└────────────────────────────────────────────────────┘
```

---

## 关键模块详解

### 1. 播放器API层 (`ijkplayer.h/c`)

**职责**: 对外提供播放器接口

**主要API**:
```c
// 全局初始化
void ijkmp_global_init();
void ijkmp_global_uninit();

// 创建播放器
IjkMediaPlayer *ijkmp_create(int (*msg_loop)(void*));
void ijkmp_dec_ref(IjkMediaPlayer *mp);

// 播放控制
int ijkmp_set_data_source(IjkMediaPlayer *mp, const char *url);
int ijkmp_prepare_async(IjkMediaPlayer *mp);
int ijkmp_start(IjkMediaPlayer *mp);
int ijkmp_pause(IjkMediaPlayer *mp);
int ijkmp_stop(IjkMediaPlayer *mp);
int ijkmp_seek_to(IjkMediaPlayer *mp, long msec);

// 状态查询
int  ijkmp_get_state(IjkMediaPlayer *mp);
bool ijkmp_is_playing(IjkMediaPlayer *mp);
long ijkmp_get_current_position(IjkMediaPlayer *mp);
long ijkmp_get_duration(IjkMediaPlayer *mp);

// 选项设置
void ijkmp_set_option(IjkMediaPlayer *mp, int opt_category, 
                      const char *name, const char *value);
void ijkmp_set_option_int(IjkMediaPlayer *mp, int opt_category, 
                          const char *name, int64_t value);
```

**状态机**:
```
IDLE → INITIALIZED → PREPARING → PREPARED → STARTED
                                    ↓         ↓
                                  PAUSED ← COMPLETED
                                    ↓
                                  STOPPED
```

### 2. 核心播放层 (`ff_ffplay.c/h`)

**职责**: 实现播放核心逻辑（改自 FFmpeg 的 ffplay.c）

**核心结构**:
```c
typedef struct FFPlayer {
    // 播放状态
    VideoState *is;
    
    // 消息队列
    MessageQueue msg_queue;
    
    // 解码配置
    AVDictionary *format_opts;
    AVDictionary *codec_opts;
    
    // 音视频输出
    SDL_Vout *vout;
    SDL_Aout *aout;
    
    // 缓冲控制
    int buffering_on;
    int buffering_count;
    
    // 同步参数
    int av_sync_type;
    
    // 其他配置...
} FFPlayer;

typedef struct VideoState {
    // 解码线程
    SDL_Thread *read_tid;
    SDL_Thread *audio_tid;
    SDL_Thread *video_tid;
    
    // 数据包队列
    PacketQueue audioq;
    PacketQueue videoq;
    PacketQueue subtitleq;
    
    // 帧队列
    FrameQueue pictq;
    FrameQueue sampq;
    FrameQueue subpq;
    
    // 时钟同步
    Clock audclk;
    Clock vidclk;
    Clock extclk;
    
    // 解码上下文
    AVFormatContext *ic;
    AVCodecContext *audio_ctx;
    AVCodecContext *video_ctx;
    
    // 其他...
} VideoState;
```

**核心线程**:
1. **read_thread**: 读取数据包，放入队列
2. **audio_thread**: 解码音频
3. **video_thread**: 解码视频
4. **subtitle_thread**: 解码字幕

### 3. SDL抽象层 (`ijksdl/`)

#### 视频输出接口 (`ijksdl_vout.h`)
```c
typedef struct SDL_Vout {
    SDL_Vout_Opaque *opaque;  // 平台私有数据
    
    // 创建覆盖层（用于渲染）
    SDL_VoutOverlay *(*create_overlay)(int w, int h, 
                                       int format, SDL_Vout *vout);
    
    // 显示帧
    int (*display_overlay)(SDL_Vout *vout, SDL_VoutOverlay *overlay);
    
    // 释放资源
    void (*free_l)(SDL_Vout *vout);
} SDL_Vout;

typedef struct SDL_VoutOverlay {
    int w, h;           // 视频尺寸
    Uint32 format;      // 像素格式 (YUV420P等)
    int planes;         // 平面数
    Uint16 *pitches;    // 每个平面的步长
    Uint8 **pixels;     // 像素数据指针
    
    // 锁定/解锁
    int (*lock)(SDL_VoutOverlay *overlay);
    int (*unlock)(SDL_VoutOverlay *overlay);
    
    // 填充帧数据
    int (*func_fill_frame)(SDL_VoutOverlay *overlay, 
                          const AVFrame *frame);
} SDL_VoutOverlay;
```

#### 音频输出接口 (`ijksdl_aout.h`)
```c
typedef struct SDL_Aout {
    SDL_Aout_Opaque *opaque;  // 平台私有数据
    
    // 打开音频设备
    int (*open_audio)(SDL_Aout *aout, 
                      const SDL_AudioSpec *desired, 
                      SDL_AudioSpec *obtained);
    
    // 暂停/恢复
    void (*pause_audio)(SDL_Aout *aout, int pause_on);
    
    // 清空缓冲
    void (*flush_audio)(SDL_Aout *aout);
    
    // 设置音量
    void (*set_volume)(SDL_Aout *aout, float left, float right);
    
    // 关闭音频
    void (*close_audio)(SDL_Aout *aout);
    
    // 释放资源
    void (*free_l)(SDL_Aout *aout);
} SDL_Aout;
```

### 4. 平台实现层

#### Android 实现 (`ijksdl/android/`)
- **视频输出**: 
  - `ijksdl_vout_android_nativewindow.c` - 使用 ANativeWindow
  - `ijksdl_vout_android_surface.c` - 使用 Surface
  - OpenGL ES 2.0 渲染 (gles2/)
  
- **音频输出**:
  - `ijksdl_aout_android_audiotrack.c` - Java AudioTrack
  - `ijksdl_aout_android_opensles.c` - OpenSL ES

- **硬件解码**:
  - `ijksdl_codec_android_mediacodec.c` - MediaCodec 硬解

#### iOS 实现 (`ios/IJKMediaPlayer/`)
- **视频输出**: OpenGL ES 2.0
- **音频输出**: AudioQueue / AudioUnit
- **硬件解码**: VideoToolbox

---

## 数据流向

```
[用户] 调用 ijkmp_set_data_source("video.mp4")
            ↓
[ijkplayer.c] 保存 URL，切换状态到 INITIALIZED
            ↓
[用户] 调用 ijkmp_prepare_async()
            ↓
[ijkplayer.c] 启动消息线程，调用 ffp_prepare_async_l()
            ↓
[ff_ffplay.c] 创建 VideoState，启动 read_thread
            ↓
[read_thread] avformat_open_input() 打开文件
            ↓ avformat_find_stream_info() 获取流信息
            ↓
[read_thread] 打开音视频解码器，创建解码线程
            ↓
[read_thread] 循环读取 packet: av_read_frame()
            ↓
[read_thread] 将 packet 放入 audioq 或 videoq
            ↓
[audio_thread] 从 audioq 取 packet，解码成 frame
            ↓ avcodec_decode_audio4()
            ↓
[audio_thread] 将 PCM 数据送到 SDL_Aout 播放
            ↓
[SDL_Aout] → [AudioTrack/AudioQueue] → 扬声器
            ↓
[video_thread] 从 videoq 取 packet，解码成 frame
            ↓ avcodec_decode_video2()
            ↓
[video_thread] 将 frame 放入 pictq (帧队列)
            ↓
[主线程] video_refresh_timer() 从 pictq 取 frame
            ↓ 计算音视频同步延迟
            ↓
[主线程] 调用 SDL_Vout->display_overlay()
            ↓
[SDL_Vout] → [OpenGL/NativeWindow] → 屏幕
```

---

## 核心文件清单

### 必须保留的核心文件（平台无关）

```
ijkmedia/ijkplayer/
├── ijkplayer.h/c              ✅ 播放器主API
├── ijkplayer_internal.h       ✅ 内部结构定义
├── ff_ffplay.h/c              ✅ 核心播放逻辑 ⭐⭐⭐
├── ff_ffplay_def.h            ✅ 核心数据结构定义
├── ff_ffplay_options.h        ✅ 播放器选项
├── ff_ffpipeline.h/c          ✅ 解码管道抽象
├── ff_ffpipenode.h/c          ✅ 管道节点
├── ff_ffmsg.h                 ✅ 消息定义
├── ff_ffmsg_queue.h           ✅ 消息队列
├── ff_cmdutils.h/c            ✅ FFmpeg 工具函数
├── ijkmeta.h/c                ✅ 元数据管理
├── ijkavformat/               ✅ 自定义 IO 协议
│   ├── ijkio*.c               - 异步IO、缓存IO等
│   ├── ijklivehook.c          - 直播钩子
│   └── ijkurlhook.c           - URL 钩子
├── ijkavutil/                 ✅ 工具函数库
│   ├── ijkdict.c              - 字典
│   ├── ijkfifo.c              - FIFO
│   ├── ijkthreadpool.c        - 线程池
│   └── ijkutils.c             - 工具函数
└── pipeline/                  ✅ 默认管道实现
    ├── ffpipeline_ffplay.c    - 软解管道
    └── ffpipenode_ffplay_vdec.c - 软解节点

ijkmedia/ijksdl/
├── ijksdl_vout.h/c            ✅ 视频输出接口
├── ijksdl_aout.h/c            ✅ 音频输出接口
├── ijksdl_thread.h/c          ✅ 线程封装
├── ijksdl_mutex.h/c           ✅ 互斥锁封装
├── ijksdl_timer.h/c           ✅ 定时器
├── ijksdl_audio.h/c           ✅ 音频定义
├── ijksdl_video.h             ✅ 视频定义
├── ijksdl_stdinc.h/c          ✅ 标准库封装
├── ijksdl_log.h               ✅ 日志
├── ijksdl_error.h/c           ✅ 错误处理
├── ffmpeg/                    ✅ FFmpeg 相关
│   ├── ijksdl_inc_ffmpeg.h    - FFmpeg 头文件
│   └── ijksdl_vout_overlay_ffmpeg.c - overlay 实现
└── gles2/                     ✅ OpenGL ES 渲染器（可选）
    ├── renderer_yuv420p.c     - YUV420P 渲染
    ├── renderer_rgb.c         - RGB 渲染
    └── shader.c               - 着色器管理
```

### 平台特定文件（可以舍弃）

```
ijkmedia/ijkplayer/android/    ❌ Android JNI（不需要）
ijkmedia/ijksdl/android/       ❌ Android 实现（不需要）
ijkmedia/ijkj4a/               ❌ Java for Android（不需要）

android/                       ❌ Android 项目（不需要）
ios/                           ❌ iOS 项目（不需要）
```

---

## 移植到 Qt Windows 的关键点

### 需要实现的新文件

```
ijkmedia/ijksdl/windows/       🆕 Windows SDL 实现
├── ijksdl_vout_qt_opengl.cpp  - Qt OpenGL 视频渲染
├── ijksdl_aout_qt_audio.cpp   - Qt 音频输出
├── ijksdl_thread_windows.c    - Windows 线程实现（或用Qt）
└── ijksdl_mutex_windows.c     - Windows 互斥锁（或用Qt）

qt-wrapper/                    🆕 Qt 封装层
├── IJKMediaPlayer.h/cpp       - Qt 播放器类
├── IJKVideoWidget.h/cpp       - Qt 视频控件
└── IJKAudioOutput.h/cpp       - Qt 音频输出封装
```

### 需要修改的地方

1. **日志宏替换** (`config.h`)
```c
#ifdef _WIN32
#define ALOGD  printf
#define ALOGE  fprintf(stderr, ...)
#define MPTRACE printf
#endif
```

2. **线程和互斥锁** (`ijksdl_thread.c`, `ijksdl_mutex.c`)
- 选项 A: 基于 Windows API (CreateThread, CRITICAL_SECTION)
- 选项 B: 基于 Qt (QThread, QMutex) - **推荐**

3. **视频输出** (新建 `ijksdl_vout_qt_opengl.cpp`)
- 使用 `QOpenGLWidget` 或 `QOpenGLWindow`
- 实现 YUV420P → RGB 转换（OpenGL 着色器）

4. **音频输出** (新建 `ijksdl_aout_qt_audio.cpp`)
- 使用 `QAudioOutput`
- 处理音频缓冲和延迟

### 编译依赖

```cmake
# FFmpeg
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)

# Qt
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia OpenGL)

# pthread (Windows)
# 使用 pthreads-win32 或 Qt 的线程
```

---

## 移植工作量估算

| 模块 | 工作量 | 说明 |
|------|--------|------|
| 核心代码复制 | 1天 | 直接复制，不需要修改 |
| 视频输出实现 | 3-5天 | Qt OpenGL 渲染，YUV→RGB |
| 音频输出实现 | 2-3天 | Qt 音频输出，缓冲管理 |
| 线程/互斥锁适配 | 1-2天 | 基于 Qt 或 Windows API |
| Qt 封装层 | 2-3天 | C++ 类封装，信号槽 |
| 示例程序 | 1-2天 | 简单的播放器界面 |
| 调试和优化 | 5-7天 | 音视频同步、性能优化 |
| **总计** | **15-23天** | 约 3-4 周 |

---

## 优势和挑战

### ✅ 优势
1. **成熟稳定**: ijkplayer 在 Android/iOS 上久经考验
2. **功能丰富**: 支持多种格式、协议、硬件解码
3. **代码复用**: 90% 以上核心代码无需修改
4. **性能优秀**: 基于 FFmpeg，性能有保障
5. **易于扩展**: 模块化设计，便于定制

### ⚠️ 挑战
1. **SDL 层适配**: 需要熟悉 Windows 多媒体 API
2. **音视频同步**: 需要仔细调试延迟和卡顿
3. **硬件解码**: Windows 硬解（DXVA2）集成较复杂
4. **跨平台测试**: 需要在不同 Windows 版本上测试
5. **内存管理**: C/C++ 混编，需注意资源释放

---

## 替代方案对比

| 方案 | 优点 | 缺点 |
|------|------|------|
| **移植 ijkplayer** | 功能完整、性能好 | 需要适配 SDL 层 |
| **Qt Multimedia** | 原生支持、易用 | 功能有限、扩展困难 |
| **VLC libvlc** | 功能强大、跨平台 | 库体积大、集成复杂 |
| **mpv** | 轻量、现代 | C API 不够友好 |
| **从零实现** | 完全可控 | 工作量巨大（数月） |

**结论**: 移植 ijkplayer 是性价比较高的方案。

---

## 参考资源

- **ijkplayer GitHub**: https://github.com/bilibili/ijkplayer
- **FFmpeg 文档**: https://ffmpeg.org/documentation.html
- **Qt Multimedia**: https://doc.qt.io/qt-5/qtmultimedia-index.html
- **Qt OpenGL**: https://doc.qt.io/qt-5/qtopengl-index.html
- **ffplay 源码**: FFmpeg/fftools/ffplay.c (ijkplayer 的原型)

---

## 总结

ijkplayer 是一个优秀的播放器框架，采用清晰的分层架构：
- **核心播放层**（平台无关）可以完全复用
- **SDL 抽象层**提供了良好的平台适配接口
- **平台实现层**是移植的重点工作

移植到 Qt Windows 主要工作是：
1. 实现 SDL 层的 Windows 版本（视频/音频输出）
2. 创建 Qt C++ 封装层
3. 编译 FFmpeg for Windows
4. 调试和优化

预计 3-4 周可以完成基本功能，是一个可行且高效的方案。


