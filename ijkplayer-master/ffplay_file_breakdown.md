# ffplay.c 文件拆解详解

## 原始结构

**FFmpeg ffplay.c**（单文件 ~3500 行）包含：
- 数据结构定义（VideoState, PacketQueue, FrameQueue 等）
- 全局变量定义（30+ 个 static 变量）
- 队列操作函数
- 解码线程（read_thread, audio_thread, video_thread）
- 音视频同步逻辑
- SDL 渲染代码
- 播放控制函数
- main() 函数

---

## ijkplayer 拆解后的文件结构

### 📁 核心分类：8 大类别

```
ijkmedia/ijkplayer/
├── 1️⃣ 数据结构定义文件
├── 2️⃣ 核心播放逻辑文件
├── 3️⃣ 辅助工具文件
├── 4️⃣ 消息系统文件
├── 5️⃣ 平台抽象文件
├── 6️⃣ 外部 API 文件
├── 7️⃣ 扩展功能文件
└── 8️⃣ 平台特定文件
```

---

## 详细文件列表

### 1️⃣ 数据结构定义层

#### **ff_ffplay_def.h** ⭐⭐⭐⭐⭐
**来源**：从 ffplay.c 开头的结构定义提取  
**行数**：~900 行  
**作用**：
```c
// 定义所有核心数据结构（原 ffplay.c 第 1-330 行）
- MyAVPacketList      // 数据包链表节点
- PacketQueue         // 数据包队列（音频/视频/字幕）
- AudioParams         // 音频参数
- Clock               // 时钟同步结构
- Frame               // 帧结构
- FrameQueue          // 帧队列
- Decoder             // 解码器结构
- VideoState          // 播放器核心状态（原 ffplay 的核心）
- FFPlayer            // 🆕 新增：封装所有原全局变量
- FFStatistic         // 🆕 新增：统计信息
- FFDemuxCacheControl // 🆕 新增：缓冲控制
```

**关键变化**：
- ✅ 将 ffplay.c 中的所有 `typedef struct` 提取出来
- ✅ 将 30+ 个全局变量封装到 `FFPlayer` 结构体
- ✅ 添加平台扩展字段（mediacodec, videotoolbox 等）

**示例**：
```c
// 原 ffplay.c（分散定义）
typedef struct VideoState {
    SDL_Thread *read_tid;
    AVFormatContext *ic;
    // ... 100+ 字段
} VideoState;

static VideoState *cur_stream;  // 全局变量
static int audio_disable;       // 全局变量

// ff_ffplay_def.h（集中定义）
typedef struct VideoState {
    // 保持原有字段
    SDL_Thread *read_tid;
    AVFormatContext *ic;
    // ... 原有字段
    
    /* extra fields - 新增扩展 */
    SDL_mutex *play_mutex;
    int buffering_on;
    int pause_req;
    // ... 扩展字段
} VideoState;

typedef struct FFPlayer {
    VideoState *is;           // 原 cur_stream
    int audio_disable;        // 原全局变量
    int video_disable;        // 原全局变量
    
    /* extra fields - 平台扩展 */
    SDL_Aout *aout;           // 音频输出抽象
    SDL_Vout *vout;           // 视频输出抽象
    IJKFF_Pipeline *pipeline; // 解码管道
    MessageQueue msg_queue;   // 消息队列
    // ... 更多扩展
} FFPlayer;
```

---

### 2️⃣ 核心播放逻辑层

#### **ff_ffplay.h** ⭐⭐⭐⭐
**来源**：ffplay.c 的函数声明  
**行数**：~120 行  
**作用**：
```c
// 播放器核心函数声明
void ffp_global_init();                          // 全局初始化
FFPlayer *ffp_create();                          // 创建播放器
void ffp_destroy(FFPlayer *ffp);                 // 销毁播放器

int ffp_prepare_async_l(FFPlayer *ffp, ...);    // 准备播放
int ffp_start_l(FFPlayer *ffp);                  // 开始播放
int ffp_pause_l(FFPlayer *ffp);                  // 暂停播放
int ffp_stop_l(FFPlayer *ffp);                   // 停止播放
int ffp_seek_to_l(FFPlayer *ffp, long msec);    // 跳转

long ffp_get_current_position_l(FFPlayer *ffp); // 获取位置
long ffp_get_duration_l(FFPlayer *ffp);          // 获取时长

void ffp_set_playback_rate(FFPlayer *ffp, float rate);   // 倍速
void ffp_set_playback_volume(FFPlayer *ffp, float vol);  // 音量

// 内部函数
int ffp_video_thread(FFPlayer *ffp);             // 视频线程
void ffp_toggle_buffering(FFPlayer *ffp, int start); // 缓冲控制
```

**关键变化**：
- 🔄 所有函数增加 `FFPlayer *ffp` 参数（替代全局变量）
- 🔄 函数名加 `ffp_` 前缀（命名空间）
- ✅ 去掉 `static`，允许外部调用

---

#### **ff_ffplay.c** ⭐⭐⭐⭐⭐
**来源**：ffplay.c 的主体实现  
**行数**：~5000 行（比原 ffplay.c 更长，因为添加了扩展功能）  
**作用**：
```c
// 实现所有核心播放逻辑
1. 队列操作
   - packet_queue_init()      // 数据包队列初始化
   - packet_queue_put()       // 入队
   - packet_queue_get()       // 出队
   - frame_queue_*()          // 帧队列操作

2. 解码线程（保持 90% 不变）
   - read_thread()            // 读取线程（原 ffplay.c 核心）
   - audio_thread()           // 音频解码线程
   - video_thread()           // 视频解码线程
   - subtitle_decode_thread() // 字幕解码线程

3. 音视频同步
   - get_master_sync_type()   // 获取同步类型
   - get_master_clock()       // 获取主时钟
   - synchronize_audio()      // 音频同步
   - video_refresh()          // 视频刷新

4. 解码器管理
   - decoder_init()           // 初始化解码器
   - decoder_decode_frame()   // 解码帧
   - decoder_start()          // 启动解码器
   - decoder_abort()          // 中止解码器

5. 音频处理
   - audio_decode_frame()     // 音频解码
   - sdl_audio_callback()     // SDL 音频回调

6. 视频处理
   - video_image_display()    // 视频显示
   - video_refresh_thread()   // 🆕 独立视频刷新线程

7. 流控制
   - stream_open()            // 打开流（原 ffplay.c）
   - stream_close()           // 关闭流
   - stream_component_open()  // 打开音视频流
   - stream_component_close() // 关闭音视频流
   - stream_seek()            // 跳转

8. 缓冲控制（🆕 新增）
   - ffp_toggle_buffering_l() // 缓冲控制
   - ffp_check_buffering_l()  // 检查缓冲状态

9. 统计功能（🆕 新增）
   - ffp_track_statistic_l()  // 轨道统计
   - ffp_audio_statistic_l()  // 音频统计
   - ffp_video_statistic_l()  // 视频统计
```

**关键变化**：
```c
// 原 ffplay.c
static int read_thread(void *arg) {
    VideoState *is = arg;
    // 访问全局变量
    if (audio_disable)
        is->audio_stream = -1;
}

// ff_ffplay.c
static int read_thread(void *arg) {
    FFPlayer *ffp = arg;           // 🔄 改为 FFPlayer
    VideoState *is = ffp->is;      // 🔄 从 ffp 获取
    // 访问实例变量
    if (ffp->audio_disable)        // 🔄 ffp-> 而非全局
        is->audio_stream = -1;
}
```

---

### 3️⃣ 辅助工具层

#### **ff_cmdutils.h / ff_cmdutils.c** ⭐⭐⭐
**来源**：ffplay.c 和 FFmpeg 的 cmdutils.c  
**行数**：~200 行  
**作用**：
```c
// FFmpeg 命令行工具函数
- parse_number_or_die()      // 解析数字
- parse_time_or_die()        // 解析时间
- show_help_options()        // 显示帮助
- check_stream_specifier()   // 检查流说明符
- filter_codec_opts()        // 过滤编解码器选项
- setup_find_stream_info_opts() // 设置流信息查找选项
```

**用途**：处理播放器选项和参数解析

---

#### **ff_fferror.h** ⭐⭐
**来源**：新增（基于 FFmpeg 错误码）  
**行数**：~50 行  
**作用**：
```c
// 错误码定义
#define EIJK_INVALID_STATE     -10001
#define EIJK_NULL_IS_PTR       -10002
#define EIJK_OUT_OF_MEMORY     -10003
// ...
```

**用途**：统一错误码管理

---

#### **ff_ffinc.h** ⭐
**来源**：新增  
**行数**：~30 行  
**作用**：
```c
// FFmpeg 头文件集中包含
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
// ...
```

**用途**：简化 FFmpeg 头文件包含

---

### 4️⃣ 消息系统层

#### **ff_ffmsg.h** ⭐⭐⭐⭐
**来源**：新增（替代 SDL 事件）  
**行数**：~60 行  
**作用**：
```c
// 消息定义
#define FFP_MSG_FLUSH                   0
#define FFP_MSG_ERROR                   100
#define FFP_MSG_PREPARED                1
#define FFP_MSG_COMPLETED               2
#define FFP_MSG_VIDEO_SIZE_CHANGED      3
#define FFP_MSG_SAR_CHANGED             4
#define FFP_MSG_BUFFERING_START         5
#define FFP_MSG_BUFFERING_END           6
#define FFP_MSG_SEEK_COMPLETE           7
// ...

typedef struct AVMessage {
    int what;      // 消息类型
    int arg1;      // 参数1
    int arg2;      // 参数2
    void *obj;     // 对象指针
} AVMessage;

// 消息发送函数
void ffp_notify_msg1(FFPlayer *ffp, int what);
void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1);
void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2);
```

**用途**：
- 替代 SDL 事件系统
- 实现播放器内核与外部的异步通信
- 支持 Android/iOS 主线程更新 UI

---

#### **ff_ffmsg_queue.h** ⭐⭐⭐
**来源**：新增（基于 PacketQueue 改造）  
**行数**：~100 行（宏定义）  
**作用**：
```c
// 消息队列实现（宏定义）
typedef struct MessageQueue {
    AVMessage *queue;     // 消息数组
    int nb_messages;      // 消息数量
    int allocated_size;   // 分配大小
    SDL_mutex *mutex;     // 互斥锁
    SDL_cond *cond;       // 条件变量
    int abort_request;    // 中止请求
} MessageQueue;

#define msg_queue_put(q, msg)         // 放入消息
#define msg_queue_get(q, msg, block)  // 获取消息
#define msg_queue_flush(q)            // 清空队列
```

**用途**：线程安全的消息队列

---

### 5️⃣ 平台抽象层（Pipeline）

#### **ff_ffpipeline.h / ff_ffpipeline.c** ⭐⭐⭐⭐
**来源**：新增（策略模式）  
**行数**：~100 行  
**作用**：
```c
// 解码管道抽象
typedef struct IJKFF_Pipeline {
    SDL_Class *opaque_class;
    IJKFF_Pipeline_Opaque *opaque;
    
    // 函数指针 - 策略接口
    void (*func_destroy)(IJKFF_Pipeline *pipeline);
    
    // 打开视频解码器（软解/硬解）
    IJKFF_Pipenode *(*func_open_video_decoder)(
        IJKFF_Pipeline *pipeline, FFPlayer *ffp);
    
    // 打开音频输出
    SDL_Aout *(*func_open_audio_output)(
        IJKFF_Pipeline *pipeline, FFPlayer *ffp);
    
    // 初始化视频解码器
    IJKFF_Pipenode *(*func_init_video_decoder)(
        IJKFF_Pipeline *pipeline, FFPlayer *ffp);
    
    // 配置视频解码器
    int (*func_config_video_decoder)(
        IJKFF_Pipeline *pipeline, FFPlayer *ffp);
} IJKFF_Pipeline;

// 管道操作函数
IJKFF_Pipeline *ffpipeline_alloc(...);
void ffpipeline_free(IJKFF_Pipeline *pipeline);
IJKFF_Pipenode *ffpipeline_open_video_decoder(...);
SDL_Aout *ffpipeline_open_audio_output(...);
```

**用途**：
- 抽象解码器创建逻辑
- 支持运行时选择软解/硬解
- 平台扩展点（Android MediaCodec, iOS VideoToolbox）

---

#### **ff_ffpipenode.h / ff_ffpipenode.c** ⭐⭐⭐
**来源**：新增  
**行数**：~80 行  
**作用**：
```c
// 管道节点（解码器节点）抽象
typedef struct IJKFF_Pipenode {
    SDL_mutex *mutex;
    void *opaque;
    
    // 函数指针 - 解码器操作
    void (*func_destroy)(IJKFF_Pipenode *node);
    int  (*func_run_sync)(IJKFF_Pipenode *node);
    int  (*func_flush)(IJKFF_Pipenode *node);
    int  (*func_queue_pic)(IJKFF_Pipenode *node, AVFrame *frame);
} IJKFF_Pipenode;
```

**用途**：解码器节点的统一接口

---

#### **pipeline/ffpipeline_ffplay.h / ffpipeline_ffplay.c** ⭐⭐⭐
**来源**：新增（默认软解实现）  
**行数**：~100 行  
**作用**：
```c
// 软解管道实现
IJKFF_Pipeline *ffpipeline_create_from_ffplay(FFPlayer *ffp) {
    IJKFF_Pipeline *pipeline = ffpipeline_alloc(...);
    
    // 使用 FFmpeg 软件解码
    pipeline->func_open_video_decoder = func_open_video_decoder;
    pipeline->func_open_audio_output  = func_open_audio_output;
    
    return pipeline;
}

static IJKFF_Pipenode *func_open_video_decoder(...) {
    // 返回 FFmpeg 软解节点
    return ffpipenode_create_video_decoder_from_ffplay(ffp);
}
```

**用途**：提供默认的 FFmpeg 软件解码实现

---

#### **pipeline/ffpipenode_ffplay_vdec.h / ffpipenode_ffplay_vdec.c** ⭐⭐⭐
**来源**：新增  
**行数**：~150 行  
**作用**：
```c
// FFmpeg 软解节点实现
IJKFF_Pipenode *ffpipenode_create_video_decoder_from_ffplay(FFPlayer *ffp) {
    IJKFF_Pipenode *node = ffpipenode_alloc(sizeof(IJKFF_Pipenode_Opaque));
    
    node->func_destroy  = func_destroy;
    node->func_run_sync = func_run_sync;  // 调用 FFmpeg avcodec_decode_video2
    node->func_flush    = func_flush;
    
    return node;
}
```

**用途**：封装 FFmpeg 的 `avcodec_decode_video2` 调用

---

### 6️⃣ 外部 API 层

#### **ijkplayer.h / ijkplayer.c** ⭐⭐⭐⭐⭐
**来源**：新增（门面模式）  
**行数**：~800 行  
**作用**：
```c
// 对外暴露的播放器 API
typedef struct IjkMediaPlayer IjkMediaPlayer;

// 全局函数
void ijkmp_global_init();
void ijkmp_global_uninit();

// 创建/销毁
IjkMediaPlayer *ijkmp_create(int (*msg_loop)(void*));
void ijkmp_dec_ref(IjkMediaPlayer *mp);

// 播放控制
int  ijkmp_set_data_source(IjkMediaPlayer *mp, const char *url);
int  ijkmp_prepare_async(IjkMediaPlayer *mp);
int  ijkmp_start(IjkMediaPlayer *mp);
int  ijkmp_pause(IjkMediaPlayer *mp);
int  ijkmp_stop(IjkMediaPlayer *mp);
int  ijkmp_seek_to(IjkMediaPlayer *mp, long msec);

// 状态查询
int  ijkmp_get_state(IjkMediaPlayer *mp);
bool ijkmp_is_playing(IjkMediaPlayer *mp);
long ijkmp_get_current_position(IjkMediaPlayer *mp);
long ijkmp_get_duration(IjkMediaPlayer *mp);

// 选项设置
void ijkmp_set_option(IjkMediaPlayer *mp, int category, 
                      const char *name, const char *value);
void ijkmp_set_option_int(IjkMediaPlayer *mp, int category, 
                          const char *name, int64_t value);

// 消息获取
int ijkmp_get_msg(IjkMediaPlayer *mp, AVMessage *msg, int block);

// 高级功能
void ijkmp_set_playback_rate(IjkMediaPlayer *mp, float rate);
void ijkmp_set_playback_volume(IjkMediaPlayer *mp, float volume);
```

**内部结构**：
```c
struct IjkMediaPlayer {
    pthread_mutex_t mutex;
    FFPlayer *ffplayer;          // 指向核心播放器
    int mp_state;                // 播放器状态
    SDL_Thread *msg_thread;      // 消息线程
    
    char *data_source;           // 数据源
    void *weak_thiz;             // 弱引用（JNI 使用）
    
    int seek_req;
    long seek_msec;
    // ...
};
```

**用途**：
- 提供简洁的外部 API
- 隐藏 ff_ffplay.c 的复杂性
- 管理播放器状态机
- 线程安全封装

---

### 7️⃣ 扩展功能层

#### **ijkmeta.h / ijkmeta.c** ⭐⭐⭐
**来源**：新增  
**行数**：~200 行  
**作用**：
```c
// 媒体元数据管理
typedef struct IjkMediaMeta IjkMediaMeta;

IjkMediaMeta *ijkmeta_create();
void ijkmeta_destroy(IjkMediaMeta *meta);
void ijkmeta_destroy_p(IjkMediaMeta **meta);

// 设置元数据
void ijkmeta_set_int64_l(IjkMediaMeta *meta, const char *name, int64_t value);
void ijkmeta_set_string_l(IjkMediaMeta *meta, const char *name, const char *value);

// 获取元数据
const char *ijkmeta_get_string_l(IjkMediaMeta *meta, const char *name);
int64_t ijkmeta_get_int64_l(IjkMediaMeta *meta, const char *name, int64_t defaultValue);

// 元数据项
typedef struct IjkMediaMeta_Item {
    char *key;
    char *value;
} IjkMediaMeta_Item;
```

**用途**：
- 存储视频标题、作者、时长等信息
- 流信息（分辨率、编码格式、码率）
- 提供给上层应用展示

---

#### **ijkavformat/** 目录 ⭐⭐⭐
**来源**：新增（自定义 IO 协议）  
**文件**：
```
ijkavformat/
├── ijkavformat.h           // 格式处理头文件
├── ijkio*.c                // 自定义 IO 实现
│   ├── ijkio.c             // IO 基础
│   ├── ijkioandroidio.c    // Android 特定 IO
│   ├── ijkioapplication.c  // 应用层 IO
│   ├── ijkiocache.c        // 缓存 IO
│   ├── ijkioffio.c         // 文件 IO
│   └── ijkioprotocol.c     // 协议处理
├── ijkiomanager.c          // IO 管理器
├── ijkurlhook.c            // URL 钩子
├── ijklivehook.c           // 直播钩子
├── ijklas.c                // LAS 协议
├── ijksegment.c            // 分段处理
└── cJSON.c/h               // JSON 解析
```

**作用**：
- 扩展 FFmpeg 的 IO 能力
- 支持自定义协议（如私有协议）
- 实现 HTTP 缓存、断点续传
- 直播流优化（低延迟、快速启动）

---

#### **ijkavutil/** 目录 ⭐⭐
**来源**：新增（工具库）  
**文件**：
```
ijkavutil/
├── ijkdict.c/h           // 字典（键值对）
├── ijkfifo.c/h           // FIFO 队列
├── ijkthreadpool.c/h     // 线程池
├── ijktree.c/h           // 树结构
├── ijkutils.c/h          // 工具函数
├── ijkstl.cpp/h          // STL 封装
└── opt.h                 // 选项定义
```

**作用**：通用数据结构和工具函数

---

### 8️⃣ 平台特定文件

#### **android/ijkplayer_android.h / ijkplayer_android.c** ⭐⭐⭐
**来源**：新增（Android 适配）  
**行数**：~150 行  
**作用**：
```c
// Android 平台初始化
IjkMediaPlayer *ijkmp_android_create(int (*msg_loop)(void*)) {
    IjkMediaPlayer *mp = ijkmp_create(msg_loop);
    
    // 创建 Android Pipeline（支持 MediaCodec 硬解）
    mp->ffplayer->pipeline = ffpipeline_create_from_android(mp->ffplayer);
    
    // 创建 Android 音频输出（AudioTrack）
    mp->ffplayer->aout = SDL_AoutAndroid_CreateForAudioTrack();
    
    // 创建 Android 视频输出（NativeWindow）
    mp->ffplayer->vout = SDL_VoutAndroid_CreateForAndroidSurface();
    
    return mp;
}
```

**用途**：Android 平台的初始化和配置

---

#### **android/ijkplayer_jni.c** ⭐⭐⭐⭐
**来源**：新增（JNI 绑定）  
**行数**：~1200 行  
**作用**：
```c
// JNI 函数绑定（Java ↔ C）
JNIEXPORT jlong JNICALL
Java_tv_danmaku_ijk_media_player_IjkMediaPlayer__1native_1setup(
    JNIEnv *env, jobject thiz, jobject weak_thiz) {
    IjkMediaPlayer *mp = ijkmp_android_create(message_loop);
    // ...
    return (jlong)(intptr_t)mp;
}

JNIEXPORT void JNICALL
Java_tv_danmaku_ijk_media_player_IjkMediaPlayer__1setDataSource(
    JNIEnv *env, jobject thiz, jlong mp_ptr, jstring url) {
    IjkMediaPlayer *mp = (IjkMediaPlayer *)(intptr_t)mp_ptr;
    const char *c_url = (*env)->GetStringUTFChars(env, url, NULL);
    ijkmp_set_data_source(mp, c_url);
    // ...
}

// 对应 30+ 个 Java native 方法
```

**用途**：
- Java 层与 C 层桥接
- 线程安全处理
- JNI 资源管理

---

#### **android/pipeline/** ⭐⭐⭐⭐
**来源**：新增（Android 硬解）  
**文件**：
```
android/pipeline/
├── ffpipeline_android.c          // Android 管道实现
├── ffpipenode_android_mediacodec_vdec.c  // MediaCodec 硬解节点
├── h264_nal.h                    // H.264 NAL 单元解析
├── hevc_nal.h                    // HEVC NAL 单元解析
└── mpeg4_esds.h                  // MPEG-4 ESDS 解析
```

**作用**：
```c
// Android 管道（支持硬解）
IJKFF_Pipeline *ffpipeline_create_from_android(FFPlayer *ffp) {
    IJKFF_Pipeline *pipeline = ffpipeline_alloc(...);
    
    pipeline->func_open_video_decoder = func_open_video_decoder;
    return pipeline;
}

static IJKFF_Pipenode *func_open_video_decoder(...) {
    // 检查是否支持硬解
    if (ffp->mediacodec_avc && is_h264(codec_id)) {
        // 使用 MediaCodec 硬解
        return ffpipenode_create_video_decoder_from_android_mediacodec(...);
    }
    
    // 降级到软解
    return ffpipenode_create_video_decoder_from_ffplay(ffp);
}
```

---

## 文件数量统计

| 类别 | 文件数 | 说明 |
|------|--------|------|
| **数据结构定义** | 1 | ff_ffplay_def.h |
| **核心播放逻辑** | 2 | ff_ffplay.h/c |
| **辅助工具** | 6 | ff_cmdutils, ff_fferror, ff_ffinc 等 |
| **消息系统** | 2 | ff_ffmsg.h, ff_ffmsg_queue.h |
| **平台抽象** | 6 | ff_ffpipeline, ff_ffpipenode, pipeline/* |
| **外部 API** | 2 | ijkplayer.h/c |
| **扩展功能** | 20+ | ijkmeta, ijkavformat/*, ijkavutil/* |
| **Android 平台** | 10+ | android/*, android/pipeline/* |
| **总计** | **50+** | vs 原 ffplay.c (1 个文件) |

---

## 文件关系图

```
┌─────────────────────────────────────────────────┐
│        应用层 (Android/iOS App)                 │
└─────────────────────────────────────────────────┘
                    ↓ 调用
┌─────────────────────────────────────────────────┐
│  ijkplayer_jni.c (JNI) / ObjC Bridge           │
└─────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────┐
│  ijkplayer.h/c ← 门面 API                      │
│  - 状态机管理                                   │
│  - 消息循环                                     │
└─────────────────────────────────────────────────┘
                    ↓ 调用
┌─────────────────────────────────────────────────┐
│  ff_ffplay.h/c ← 核心播放逻辑                   │
│  - read_thread, audio_thread, video_thread     │
│  - 音视频同步                                   │
└─────────────────────────────────────────────────┘
         ↓ 使用                    ↓ 依赖
┌───────────────────┐    ┌───────────────────────┐
│ ff_ffplay_def.h   │    │  ff_ffpipeline.h/c    │
│ - VideoState      │    │  - 解码管道抽象        │
│ - FFPlayer        │    │  ff_ffpipenode.h/c    │
│ - PacketQueue     │    │  - 解码节点抽象        │
└───────────────────┘    └───────────────────────┘
                                  ↓ 实现
         ┌────────────────────────┴────────────────────────┐
         ↓                                                 ↓
┌───────────────────────┐              ┌───────────────────────┐
│ ffpipeline_ffplay.c   │              │ ffpipeline_android.c  │
│ - FFmpeg 软解          │              │ - MediaCodec 硬解     │
└───────────────────────┘              └───────────────────────┘
```

---

## 对比总结

### 原 ffplay.c（单体）
```
ffplay.c (3500 行)
  ├── 结构定义 (300 行)
  ├── 全局变量 (50 行)
  ├── 队列操作 (200 行)
  ├── 解码线程 (1500 行)
  ├── 同步逻辑 (500 行)
  ├── SDL 渲染 (300 行)
  ├── 控制函数 (500 行)
  └── main() (150 行)
```

### ijkplayer（模块化）
```
50+ 个文件 (8000+ 行)
  ├── ff_ffplay_def.h (900)      ← 结构定义
  ├── ff_ffplay.c (5000)         ← 核心逻辑
  ├── ijkplayer.c (800)          ← API 门面
  ├── ff_ffpipeline.* (200)     ← 平台抽象
  ├── ff_ffmsg.* (100)           ← 消息系统
  ├── ijkmeta.* (200)            ← 元数据
  ├── ijkavformat/* (1000+)      ← 自定义 IO
  ├── ijkavutil/* (300+)         ← 工具库
  └── android/* (1500+)          ← 平台绑定
```

---

## 关键优势

| 方面 | ffplay.c | ijkplayer |
|------|----------|-----------|
| **可维护性** | 单文件 3500 行 | 模块化 50+ 文件 |
| **可扩展性** | 修改源码 | Pipeline 插件 |
| **跨平台** | Linux/macOS | Android/iOS/Windows |
| **硬件解码** | ❌ | ✅ MediaCodec/VideoToolbox |
| **多实例** | ❌ 全局变量 | ✅ 实例化 |
| **测试性** | 难以测试 | 可模拟/注入 |

这就是 ijkplayer 对 ffplay.c 的完整拆解！通过这种模块化设计，ijkplayer 既保留了 ffplay 的核心播放能力，又获得了跨平台、可扩展、易维护的优势。

