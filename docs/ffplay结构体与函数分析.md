# ffplay.c 结构体与函数分析

## 一、结构体总览

| 结构体 | 行号 | 说明 |
|--------|------|------|
| `MyAVPacketList` | 114 | 压缩数据包节点 |
| `PacketQueue` | 119 | 压缩数据包队列 |
| `AudioParams` | 135 | 音频参数 |
| `Clock` | 143 | 时钟同步 |
| `Frame` | 154 | 解码后的帧 |
| `FrameQueue` | 170 | 解码帧队列 |
| `Decoder` | 191 | 解码器 |
| `VideoState` | 206 | 播放器全局状态 |
| `TextureFormatEntry` | 388 | SDL纹理格式映射 |

---

## 二、结构体详解

### 1. MyAVPacketList (行 114-117)

```c
typedef struct MyAVPacketList {
    AVPacket *pkt;      // 解封装后的数据
    int serial;         // 播放序列，来自 PacketQueue 的 serial
} MyAVPacketList;
```

**用途**: PacketQueue 队列中的节点，封装 AVPacket 并附加 serial。

---

### 2. PacketQueue (行 119-128)

```c
typedef struct PacketQueue {
    AVFifo *pkt_list;       // 底层 FIFO 存储
    int nb_packets;         // 包数量
    int size;               // 数据总大小 (字节)
    int64_t duration;       // 总时长
    int abort_request;      // =1 请求退出
    int serial;             // 播放序列 (seek 时递增)
    SDL_mutex *mutex;       // 互斥锁
    SDL_cond *cond;         // 条件变量
} PacketQueue;
```

**用途**: 存储解封装后的压缩数据包，用于解码器输入。

**相关函数**:
| 函数 | 行号 | 说明 |
|------|------|------|
| `packet_queue_put_private` | 434 | 内部入队实现 |
| `packet_queue_put` | 459 | 入队 |
| `packet_queue_put_nullpacket` | 482 | 入队空包 (表示 EOF) |
| `packet_queue_init` | 489 | 初始化队列 |
| `packet_queue_flush` | 509 | 清空队列 |
| `packet_queue_destroy` | 523 | 销毁队列 |
| `packet_queue_abort` | 531 | 中止队列 |
| `packet_queue_start` | 542 | 启动队列 |
| `packet_queue_get` | 551 | 出队 |

---

### 3. AudioParams (行 135-141)

```c
typedef struct AudioParams {
    int freq;                   // 采样率
    AVChannelLayout ch_layout;  // 通道布局
    enum AVSampleFormat fmt;    // 采样格式
    int frame_size;             // 一个采样单元字节数
    int bytes_per_sec;          // 每秒字节数 (码率)
} AudioParams;
```

**用途**: 描述音频参数，用于重采样配置。

---

### 4. Clock (行 143-151)

```c
typedef struct Clock {
    double pts;           // 当前 PTS (秒)
    double pts_drift;     // PTS 与系统时间的偏移量
    double last_updated;  // 上次更新的系统时间
    double speed;         // 播放速度
    int serial;           // 播放序列
    int paused;           // 暂停标志
    int *queue_serial;    // 指向对应队列的 serial
} Clock;
```

**用途**: 音视频同步时钟，跟踪媒体播放位置。

**相关函数**:
| 函数 | 行号 | 说明 |
|------|------|------|
| `get_clock` | 1420 | 获取时钟当前值 |
| `set_clock_at` | 1432 | 设置时钟 (指定时间) |
| `set_clock` | 1440 | 设置时钟 |
| `set_clock_speed` | 1446 | 设置播放速度 |
| `init_clock` | 1452 | 初始化时钟 |
| `sync_clock_to_slave` | 1460 | 同步时钟到从时钟 |
| `get_master_sync_type` | 1468 | 获取主同步类型 |
| `get_master_clock` | 1485 | 获取主时钟值 |
| `check_external_clock_speed` | 1503 | 检查外部时钟速度 |

---

### 5. Frame (行 154-167)

```c
typedef struct Frame {
    AVFrame *frame;       // 数据帧
    AVSubtitle sub;       // 字幕数据
    int serial;           // 播放序列
    double pts;           // 时间戳 (秒)
    double duration;      // 持续时间 (秒)
    int64_t pos;          // 文件字节位置
    int width;            // 宽度
    int height;           // 高度
    int format;           // 像素/采样格式
    AVRational sar;       // 宽高比
    int uploaded;         // 是否已上传到纹理
    int flip_v;           // 是否垂直翻转
} Frame;
```

**用途**: 解码后的帧数据，统一音频、视频、字幕。

---

### 6. FrameQueue (行 170-183)

```c
typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];  // 帧数组
    int rindex;                     // 读索引
    int windex;                     // 写索引
    int size;                       // 当前帧数
    int max_size;                   // 最大帧数
    int keep_last;                  // 是否保留最后一帧
    int rindex_shown;               // 当前帧偏移 (0 或 1)
    SDL_mutex *mutex;
    SDL_cond *cond;
    PacketQueue *pktq;              // 关联的 PacketQueue
} FrameQueue;
```

**用途**: 解码后的帧队列，用于渲染线程读取。

**相关函数**:
| 函数 | 行号 | 说明 |
|------|------|------|
| `frame_queue_unref_item` | 705 | 释放帧资源 |
| `frame_queue_init` | 711 | 初始化队列 |
| `frame_queue_destory` | 732 | 销毁队列 |
| `frame_queue_signal` | 745 | 发送信号 |
| `frame_queue_peek` | 753 | 获取当前帧 |
| `frame_queue_peek_next` | 759 | 获取下一帧 |
| `frame_queue_peek_last` | 765 | 获取上一帧 |
| `frame_queue_peek_writable` | 771 | 获取可写位置 |
| `frame_queue_peek_readable` | 788 | 获取可读位置 |
| `frame_queue_push` | 805 | 入队 |
| `frame_queue_next` | 816 | 推进读指针 |
| `frame_queue_nb_remaining` | 833 | 获取剩余帧数 |
| `frame_queue_last_pos` | 840 | 获取最后帧位置 |

---

### 7. Decoder (行 191-204)

```c
typedef struct Decoder {
    AVPacket *pkt;                  // 当前包
    PacketQueue *queue;             // 输入队列
    AVCodecContext *avctx;          // 解码器上下文
    int pkt_serial;                 // 包序列
    int finished;                   // 解码完成标志
    int packet_pending;             // 是否有待处理的包
    SDL_cond *empty_queue_cond;     // 队列空信号 (即 continue_read_thread)
    int64_t start_pts;              // 起始 PTS
    AVRational start_pts_tb;        // 起始 PTS 时间基
    int64_t next_pts;               // 下一帧 PTS
    AVRational next_pts_tb;         // 下一帧时间基
    SDL_Thread *decoder_tid;        // 解码线程句柄
} Decoder;
```

**用途**: 封装解码器及其状态。

**相关函数**:
| 函数 | 行号 | 说明 |
|------|------|------|
| `decoder_init` | 586 | 初始化解码器 |
| `decoder_decode_frame` | 599 | 解码一帧 |
| `decoder_destroy` | 699 | 销毁解码器 |
| `decoder_abort` | 849 | 中止解码器 |
| `decoder_start` | 2209 | 启动解码线程 |

---

### 8. VideoState (行 206-321)

```c
typedef struct VideoState {
    // === 线程 ===
    SDL_Thread *read_tid;           // 读线程句柄
    SDL_cond *continue_read_thread; // 读线程唤醒条件

    // === 输入 ===
    const AVInputFormat *iformat;   // 输入格式
    AVFormatContext *ic;            // 格式上下文
    char *filename;                 // 文件名
    int realtime;                   // 是否实时流

    // === 控制状态 ===
    int abort_request;              // 退出请求
    int force_refresh;              // 强制刷新
    int paused;                     // 暂停状态
    int last_paused;                // 上次暂停状态
    int step;                       // 单步模式
    int eof;                        // 文件结束

    // === Seek 相关 ===
    int seek_req;                   // seek 请求
    int seek_flags;                 // seek 标志
    int64_t seek_pos;               // seek 目标位置
    int64_t seek_rel;               // seek 相对偏移
    int queue_attachments_req;      // 附加图片请求

    // === 时钟 ===
    Clock audclk, vidclk, extclk;   // 音频/视频/外部时钟
    int av_sync_type;               // 同步类型

    // === 音频 ===
    int audio_stream;               // 音频流索引
    AVStream *audio_st;             // 音频流
    PacketQueue audioq;             // 音频包队列
    FrameQueue sampq;               // 音频帧队列
    Decoder auddec;                 // 音频解码器
    double audio_clock;             // 音频时钟
    int audio_clock_serial;         // 音频时钟序列
    struct AudioParams audio_src;   // 源音频参数
    struct AudioParams audio_tgt;   // 目标音频参数
    struct SwrContext *swr_ctx;     // 重采样上下文
    uint8_t *audio_buf, *audio_buf1;
    unsigned int audio_buf_size, audio_buf1_size;
    int audio_buf_index, audio_write_buf_size;
    int audio_hw_buf_size;          // SDL 音频缓冲区大小
    int audio_volume;               // 音量
    int muted;                      // 静音

    // === 视频 ===
    int video_stream;               // 视频流索引
    AVStream *video_st;             // 视频流
    PacketQueue videoq;             // 视频包队列
    FrameQueue pictq;               // 视频帧队列
    Decoder viddec;                 // 视频解码器
    double frame_timer;             // 帧定时器
    double frame_last_returned_time;
    double frame_last_filter_delay;
    double max_frame_duration;      // 最大帧时长
    struct SwsContext *img_convert_ctx;  // 图像转换
    int frame_drops_early;          // 早期丢帧数
    int frame_drops_late;           // 晚期丢帧数
    SDL_Texture *vid_texture;       // 视频纹理

    // === 字幕 ===
    int subtitle_stream;            // 字幕流索引
    AVStream *subtitle_st;          // 字幕流
    PacketQueue subtitleq;          // 字幕包队列
    FrameQueue subpq;               // 字幕帧队列
    Decoder subdec;                 // 字幕解码器
    struct SwsContext *sub_convert_ctx;
    SDL_Texture *sub_texture;       // 字幕纹理

    // === 显示 ===
    int width, height, xleft, ytop; // 窗口尺寸
    enum ShowMode show_mode;        // 显示模式
    SDL_Texture *vis_texture;       // 音频可视化纹理

    // === 音频可视化 ===
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    RDFTContext *rdft;
    FFTSample *rdft_data;

    // === 滤镜 (可选) ===
    AVFilterContext *in_video_filter, *out_video_filter;
    AVFilterContext *in_audio_filter, *out_audio_filter;
    AVFilterGraph *agraph;

    // === 流切换 ===
    int last_video_stream, last_audio_stream, last_subtitle_stream;
} VideoState;
```

**用途**: 播放器全局状态，包含所有播放相关信息。

---

## 三、函数分类

### 1. PacketQueue 相关函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `packet_queue_put_private` | 434 | 内部入队 (已持有锁) |
| `packet_queue_put` | 459 | 入队 AVPacket |
| `packet_queue_put_nullpacket` | 482 | 入队空包 (EOF 信号) |
| `packet_queue_init` | 489 | 初始化 |
| `packet_queue_flush` | 509 | 清空并增加 serial |
| `packet_queue_destroy` | 523 | 销毁 |
| `packet_queue_abort` | 531 | 设置中止标志 |
| `packet_queue_start` | 542 | 启动队列 |
| `packet_queue_get` | 551 | 出队 (阻塞/非阻塞) |

---

### 2. FrameQueue 相关函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `frame_queue_unref_item` | 705 | 释放帧资源 |
| `frame_queue_init` | 711 | 初始化 |
| `frame_queue_destory` | 732 | 销毁 |
| `frame_queue_signal` | 745 | 发送条件信号 |
| `frame_queue_peek` | 753 | 获取当前帧 (rindex + rindex_shown) |
| `frame_queue_peek_next` | 759 | 获取下一帧 |
| `frame_queue_peek_last` | 765 | 获取上一帧 (rindex) |
| `frame_queue_peek_writable` | 771 | 获取可写位置 (阻塞等待) |
| `frame_queue_peek_readable` | 788 | 获取可读位置 (阻塞等待) |
| `frame_queue_push` | 805 | 入队 (windex++) |
| `frame_queue_next` | 816 | 出队 (rindex++) |
| `frame_queue_nb_remaining` | 833 | 获取剩余帧数 |
| `frame_queue_last_pos` | 840 | 获取最后帧的文件位置 |

---

### 3. Decoder 相关函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `decoder_init` | 586 | 初始化解码器结构 |
| `decoder_decode_frame` | 599 | 解码一帧 (send/receive 循环) |
| `decoder_destroy` | 699 | 销毁解码器 |
| `decoder_abort` | 849 | 中止解码 (flush 队列) |
| `decoder_start` | 2209 | 启动解码线程 |

---

### 4. Clock 相关函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `get_clock` | 1420 | 获取时钟当前值 |
| `set_clock_at` | 1432 | 设置时钟 (指定系统时间) |
| `set_clock` | 1440 | 设置时钟 |
| `set_clock_speed` | 1446 | 设置播放速度 |
| `init_clock` | 1452 | 初始化时钟 (pts=NAN) |
| `sync_clock_to_slave` | 1460 | 同步主时钟到从时钟 |
| `get_master_sync_type` | 1468 | 获取主同步类型 |
| `get_master_clock` | 1485 | 获取主时钟值 |
| `check_external_clock_speed` | 1503 | 检查并调整外部时钟速度 |

---

### 5. SDL 封装函数

#### 5.1 纹理与渲染

| 函数 | 行号 | 说明 |
|------|------|------|
| `realloc_texture` | 869 | 重新分配 SDL 纹理 |
| `calculate_display_rect` | 893 | 计算显示矩形 (保持宽高比) |
| `get_sdl_pix_fmt_and_blendmode` | 920 | 获取 SDL 像素格式和混合模式 |
| `upload_texture` | 938 | 上传帧数据到纹理 |
| `set_sdl_yuv_conversion_mode` | 991 | 设置 YUV 转换模式 |
| `fill_rectangle` (inline) | 858 | 填充矩形 |

#### 5.2 窗口与显示

| 函数 | 行号 | 说明 |
|------|------|------|
| `set_default_window_size` | 1368 | 设置默认窗口大小 |
| `video_open` | 1380 | 打开/创建视频窗口 |
| `video_display` | 1404 | 显示一帧 |
| `video_image_display` | 1007 | 显示视频画面 |
| `video_audio_display` | 1106 | 显示音频波形/频谱 |
| `toggle_full_screen` | 3436 | 切换全屏 |

#### 5.3 音频

| 函数 | 行号 | 说明 |
|------|------|------|
| `audio_open` | 2618 | 打开 SDL 音频设备 |
| `sdl_audio_callback` | 2566 | SDL 音频回调函数 |

---

### 6. FFmpeg 封装函数

#### 6.1 滤镜相关

| 函数 | 行号 | 说明 |
|------|------|------|
| `configure_filtergraph` | 1896 | 配置滤镜图 |
| `configure_video_filters` | 1939 | 配置视频滤镜 |
| `configure_audio_filters` | 2044 | 配置音频滤镜 |
| `opt_add_vfilter` | 415 | 添加视频滤镜选项 |

#### 6.2 流操作

| 函数 | 行号 | 说明 |
|------|------|------|
| `stream_component_open` | 2696 | 打开流组件 (查找解码器、初始化) |
| `stream_component_close` | 1248 | 关闭流组件 |
| `stream_has_enough_packets` | 2872 | 检查缓冲区是否足够 |
| `is_realtime` | 2880 | 判断是否实时流 |
| `decode_interrupt_cb` | 2866 | FFmpeg I/O 中断回调 |

---

### 7. 播放控制函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `stream_seek` | 1518 | 发起 seek 请求 |
| `stream_toggle_pause` | 1533 | 切换暂停状态 |
| `toggle_pause` | 1547 | 切换暂停 (用户触发) |
| `toggle_mute` | 1554 | 切换静音 |
| `update_volume` | 1560 | 更新音量 |
| `step_to_next_frame` | 1568 | 单步播放 |
| `toggle_audio_display` | 3442 | 切换音频显示模式 |
| `seek_chapter` | 3477 | 章节跳转 |
| `stream_cycle_channel` | 3357 | 切换音视频/字幕流 |

---

### 8. 音视频同步函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `compute_target_delay` | 1577 | 计算目标延迟 |
| `vp_duration` | 1616 | 计算帧持续时间 |
| `update_video_pts` | 1628 | 更新视频时钟 |
| `synchronize_audio` | 2396 | 音频同步 (调整采样数) |
| `update_sample_display` | 2375 | 更新音频显示数据 |

---

### 9. 解码/播放线程函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `read_thread` | 2898 | 读取线程 (demux) |
| `video_thread` | 2221 | 视频解码线程 |
| `audio_thread` | 2119 | 音频解码线程 |
| `subtitle_thread` | 2340 | 字幕解码线程 |
| `video_refresh` | 1635 | 视频刷新 (主循环调用) |
| `audio_decode_frame` | 2444 | 解码一帧音频 |
| `get_video_frame` | 1860 | 获取视频帧 (含丢帧) |
| `queue_picture` | 1827 | 视频帧入队 |

---

### 10. 主流程函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `stream_open` | 3288 | 打开媒体流 (创建 VideoState) |
| `stream_close` | 1305 | 关闭媒体流 |
| `event_loop` | 3505 | SDL 事件循环 |
| `refresh_loop_wait_event` | 3454 | 刷新循环等待事件 |
| `do_exit` | 1342 | 退出程序 |
| `sigterm_handler` | 1363 | 信号处理 |

---

### 11. 命令行选项函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `opt_width` | 3706 | 设置宽度 |
| `opt_height` | 3712 | 设置高度 |
| `opt_format` | 3718 | 设置格式 |
| `opt_sync` | 3728 | 设置同步类型 |
| `opt_seek` | 3743 | 设置起始位置 |
| `opt_duration` | 3749 | 设置播放时长 |
| `opt_show_mode` | 3755 | 设置显示模式 |
| `opt_input_file` | 3764 | 设置输入文件 |
| `opt_codec` | 3777 | 设置编解码器 |
| `show_usage` | 3854 | 显示使用帮助 |

---

### 12. 辅助函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `cmp_audio_fmts` | 424 | 比较音频格式 |

---

## 四、函数关系图

### 主线程流程

```
main()
  │
  ├── SDL_Init()
  ├── stream_open()           // 创建 VideoState
  │     ├── frame_queue_init() × 3
  │     ├── packet_queue_init() × 3
  │     ├── init_clock() × 3
  │     └── SDL_CreateThread(read_thread)
  │
  └── event_loop()            // 主事件循环
        └── refresh_loop_wait_event()
              └── video_refresh()
                    ├── compute_target_delay()
                    ├── update_video_pts()
                    └── video_display()
```

### 读取线程

```
read_thread()
  │
  ├── avformat_open_input()
  ├── avformat_find_stream_info()
  ├── stream_component_open() × 3
  │     ├── avcodec_find_decoder()
  │     ├── avcodec_open2()
  │     ├── decoder_init()
  │     ├── decoder_start()
  │     │     └── SDL_CreateThread(video/audio/subtitle_thread)
  │     └── audio_open() [仅音频]
  │
  └── for (;;)
        ├── avformat_seek_file() [seek 处理]
        │     └── packet_queue_flush() × 3
        │
        ├── av_read_frame()
        └── packet_queue_put()
```

### 解码线程

```
video_thread() / audio_thread() / subtitle_thread()
  │
  └── for (;;)
        ├── decoder_decode_frame()
        │     ├── packet_queue_get()
        │     ├── avcodec_send_packet()
        │     └── avcodec_receive_frame()
        │
        ├── [滤镜处理]
        │     ├── av_buffersrc_add_frame()
        │     └── av_buffersink_get_frame()
        │
        └── frame_queue_push()
              └── queue_picture() [视频]
```

### SDL 音频回调

```
sdl_audio_callback()
  │
  └── audio_decode_frame()
        ├── frame_queue_peek_readable()
        ├── synchronize_audio()
        ├── swr_convert() [重采样]
        ├── frame_queue_next()
        └── set_clock() [更新音频时钟]
```

---

## 五、数据流向

```
┌─────────────────────────────────────────────────────────────────────┐
│                         数据流向图                                   │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────┐     ┌─────────────┐     ┌─────────────┐               │
│  │ 媒体文件 │────►│ read_thread │────►│ PacketQueue │               │
│  └─────────┘     │ (demux)     │     │ (audioq/    │               │
│                  └─────────────┘     │  videoq/    │               │
│                                      │  subtitleq) │               │
│                                      └──────┬──────┘               │
│                                             │                       │
│                                             ▼                       │
│                                      ┌─────────────┐               │
│                                      │   Decoder   │               │
│                                      │ (audio/video│               │
│                                      │  /subtitle  │               │
│                                      │   thread)   │               │
│                                      └──────┬──────┘               │
│                                             │                       │
│                                             ▼                       │
│                                      ┌─────────────┐               │
│                                      │ FrameQueue  │               │
│                                      │ (sampq/     │               │
│                                      │  pictq/     │               │
│                                      │  subpq)     │               │
│                                      └──────┬──────┘               │
│                              ┌──────────────┼──────────────┐       │
│                              ▼              ▼              ▼       │
│                       ┌──────────┐   ┌──────────┐   ┌──────────┐  │
│                       │ SDL Audio│   │video_    │   │ 字幕渲染 │  │
│                       │ Callback │   │refresh() │   │          │  │
│                       └────┬─────┘   └────┬─────┘   └────┬─────┘  │
│                            │              │              │         │
│                            ▼              ▼              ▼         │
│                       ┌─────────────────────────────────────────┐  │
│                       │              SDL 输出                    │  │
│                       │         (音频播放 + 视频显示)            │  │
│                       └─────────────────────────────────────────┘  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 六、全局变量

| 变量 | 行号 | 说明 |
|------|------|------|
| `window` | 383 | SDL 窗口 |
| `renderer` | 384 | SDL 渲染器 |
| `audio_dev` | 386 | SDL 音频设备 ID |
| `file_iformat` | 324 | 输入格式 |
| `input_filename` | 325 | 输入文件名 |
| `default_width/height` | 327-328 | 默认窗口大小 |
| `audio_disable` | 333 | 禁用音频 |
| `video_disable` | 334 | 禁用视频 |
| `subtitle_disable` | 335 | 禁用字幕 |
| `seek_by_bytes` | 337 | 按字节 seek |
| `av_sync_type` | 344 | 同步类型 |
| `start_time` | 345 | 起始时间 |
| `duration` | 346 | 播放时长 |
| `framedrop` | 358 | 丢帧设置 |
| `infinite_buffer` | 359 | 无限缓冲 |
| `show_mode` | 361 | 显示模式 |
| `is_full_screen` | 378 | 全屏状态 |
| `audio_callback_time` | 379 | 音频回调时间 |

