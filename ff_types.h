/**
 * @file
 * ffplay type definitions
 */

#ifndef FF_TYPES_H
#define FF_TYPES_H

#include "ff_define.h"

typedef struct MyAVPacketList {
    AVPacket *pkt;      //解封装后的数据
    int serial;         //播放序列，它的值来自PacketQueue的serial的赋值
} MyAVPacketList;

typedef struct PacketQueue {
    AVFifo *pkt_list;
    int nb_packets;        // 包数量，也就是队列元素数量 
    int size;              // 队列所有元素的数据⼤⼩总和
    int64_t duration;      // 队列所有元素的数据播放持续时间
    int abort_request;     // =1时请求退出播放
    int serial;            // 播放序列
    SDL_mutex *mutex;      // 互斥锁
    SDL_cond *cond;        // 条件变量
} PacketQueue;

typedef struct AudioParams {
    int freq;                   // 采样率
    AVChannelLayout ch_layout;  // 通道布局，⽐如2.1声道，5.1声道等
    enum AVSampleFormat fmt;    // ⾳频采样格式，⽐如AV_SAMPLE_FMT_S16表示为有符号16bit深度，交错排列模式
    int frame_size;             // ⼀个采样单元占⽤的字节数（⽐如2通道时，则左右通道各采样⼀次合成⼀个采样单元）
    int bytes_per_sec;          // 每秒字节数，即码率⽐如采样率48Khz，2channel，16bit，则⼀秒48000*2*16/8=192000字节
} AudioParams;

typedef struct Clock {
    double pts;           // 时钟基础, 当前帧(待播放)显示时间戳，播放后，当前帧变成上⼀帧，单位为秒
    double pts_drift;     // 记录了"媒体时间与系统时间的偏移量", audio、video对于该值是独⽴的
    double last_updated;  // 最后⼀次更新的系统时钟
    double speed;         // 时钟速度控制，⽤于控制播放速度
    int serial;           // 播放序列，所谓播放序列就是⼀段连续的播放动作，⼀个seek操作会启动⼀段新的播放序列，会加1
    int paused;           // = 1 说明是暂停状态
    int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
    AVFrame *frame;       // 指向数据帧
    AVSubtitle sub;       // ⽤于字幕
    int serial;           // 播放序列，在seek的操作时serial会变化
    double pts;           // 时间戳，单位为秒
    double duration;      // 该帧持续时间，单位为秒
    int64_t pos;          // 该帧在输⼊⽂件中的字节位置
    int width;            // 图像宽度
    int height;           // 图像高度
    int format;           // 对于图像为(enum AVPixelFormat)，对于声⾳则为(enum AVSampleFormat)
    AVRational sar;       // 图像的宽⾼⽐，如果未知或未指定则为0/1, 该值来⾃AVFrame结构体的sample_aspect_ratio变量
    int uploaded;         // =1时表示图像数据已经上传到SDL纹理, 记录该帧是否已经显示过
    int flip_v;           // =1时表示图像需要垂直翻转, = 0则正常播放
} Frame;

// 环形队列，每⼀个frame_queue⼀个写端⼀个读端，写端位于解码线程，读端位于播放线程
typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];  // FRAME_QUEUE_SIZE 最⼤size, 数字太⼤时会占⽤⼤量的内存，需要注意该值的设置
    int rindex;                     // 读索引。待播放时读取此帧进⾏播放，播放后此帧成为上⼀帧
    int windex;                     // 写索引
    int size;                       // 当前总帧数
    int max_size;                   // 可存储最⼤帧数
    int keep_last;                  // =1时表示保留最后⼀帧，⽤于下⼀次seek时使⽤
    int rindex_shown;               // 当前帧相对于 rindex 的偏移量，值只有 0 或 1
                                    // =0代表rindex 位置的帧还没显示过，它就是"当前帧"
                                    // =1代表rindex 位置的帧已经显示过，它是"上一帧", 当前带渲染的帧为rindex+rindex_shown
    SDL_mutex *mutex;
    SDL_cond *cond;
    PacketQueue *pktq;
} FrameQueue;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct Decoder {
    AVPacket *pkt;
    PacketQueue *queue;                 // 数据包队列
    AVCodecContext *avctx;              // 解码器上下⽂
    int pkt_serial;                     // 包序列
    int finished;                       // =0，解码器处于⼯作状态；=⾮0，解码器处于空闲状态
    int packet_pending;                 // =0，解码器处于异常状态，需要考虑重置解码器；=1，解码器处于正常状态
    SDL_cond *empty_queue_cond;         // 检查到packet队列空时发送 signal缓存read_thread读取数据, empty_queue_cond也是is->continue_read_thread
    int64_t start_pts;                  // 初始化时是stream的start time
    AVRational start_pts_tb;            // 初始化时是stream的time_base
    int64_t next_pts;                   // 记录最近⼀次解码后的frame的pts，当解出来的部分帧没有有效的pts时则使⽤next_pts进⾏推算
    AVRational next_pts_tb;             // next_pts的时间基
    SDL_Thread *decoder_tid;            // 线程句柄
} Decoder;

typedef struct VideoState {
    SDL_Thread *read_tid;               // 读线程句柄
    const AVInputFormat *iformat;       // 指向demuxer
    int abort_request;                  // =1时请求退出播放
    int force_refresh;                  // =1时需要刷新画⾯，请求⽴即刷新画⾯的意思，如暂停时窗口尺寸变化需要刷新
    int paused;                         // =1时暂停，=0时播放
    int last_paused;                    // 暂存"暂停"/"播放"状态
    int queue_attachments_req;          // 用于请求将 内嵌封面图片 (Attached Picture) 放入视频队列进行显示
                                        // 初始化视频流、Seek 操作后会置1
    int seek_req;                       // =1, 标识进行了⼀次seek
    int seek_flags;                     // seek标志，诸如AVSEEK_FLAG_BYTE等，seek可以指定不同的行为
    int64_t seek_pos;                   // 请求seek的⽬标位置(当前位置+增量)
    int64_t seek_rel;                   // 本次seek的相对偏移量（正=前进，负=后退）
    int read_pause_return;
    AVFormatContext *ic;                // iformat的上下⽂
    int realtime;                       // =1为实时流

    Clock audclk;
    Clock vidclk;
    Clock extclk;

    FrameQueue pictq;                   // 视频Frame队列
    FrameQueue subpq;                   // 字幕Frame队列
    FrameQueue sampq;                   // 音频Frame队列

    Decoder auddec;
    Decoder viddec;
    Decoder subdec;

    int audio_stream;                   // ⾳频流索引

    int av_sync_type;                   // ⾳视频同步类型, 默认audio master

    double audio_clock;                 // 当前⾳频帧的PTS+当前帧Duration
    int audio_clock_serial;             // 播放序列，seek可改变此值
    // 以下4个参数 ⾮audio master同步⽅式使⽤
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;

    AVStream *audio_st;                 // ⾳频流
    PacketQueue audioq;                 // ⾳频packet队列
    int audio_hw_buf_size;              // SDL⾳频缓冲区的⼤⼩(字节为单位)
                                        // 指向待播放的⼀帧⾳频数据，指向的数据区将被拷⼊SDL⾳频缓冲区。若经过重采样
                                        // 则指向audio_buf1，否则指向frame中的⾳频
    uint8_t *audio_buf;                 // 指向需要重采样的数据
    uint8_t *audio_buf1;                // 指向重采样后的数据
    unsigned int audio_buf_size;        // 待播放的⼀帧⾳频数据(audio_buf指向)的⼤⼩
    unsigned int audio_buf1_size;       // 申请到的⾳频缓冲区audio_buf1的实际尺⼨
    int audio_buf_index;                // 更新拷⻉位置 当前⾳频帧中已拷⼊SDL⾳频缓冲区的位置索引(指向第⼀个待拷⻉字节)
                                        // 当前⾳频帧中尚未拷⼊SDL⾳频缓冲区的数据量:
                                        // audio_buf_size = audio_buf_index + audio_write_buf_size
    int audio_write_buf_size;
    int audio_volume;                   // ⾳量
    int muted;                          // =1静⾳，=0则正常
    struct AudioParams audio_src;       // ⾳频frame的参数
#if CONFIG_AVFILTER
    struct AudioParams audio_filter_src;
#endif
    struct AudioParams audio_tgt;       // SDL⽀持的⾳频参数，重采样转换：audio_src->audio_tgt
    struct SwrContext *swr_ctx;         // ⾳频重采样context
    int frame_drops_early;              // 丢弃视频packet计数
    int frame_drops_late;               // 丢弃视频frame计数

    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    } show_mode;
    int16_t sample_array[SAMPLE_ARRAY_SIZE];

    // ⾳频波形显示使⽤
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;
    int xpos;
    double last_vis_time;
    SDL_Texture *vis_texture;

    SDL_Texture *sub_texture;           // 字幕纹理
    SDL_Texture *vid_texture;           // 视频纹理

    int subtitle_stream;                // 字幕流索引
    AVStream *subtitle_st;              // 字幕流
    PacketQueue subtitleq;              // 字幕packet队列

    double frame_timer;                 // 记录最新的一帧播放的系统时间（系统启动后的相对时间）
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int video_stream;                   // 视频流索引
    AVStream *video_st;                 // 视频流
    PacketQueue videoq;                 // 视频队列
    double max_frame_duration;          // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    struct SwsContext *img_convert_ctx; // 视频尺⼨格式变换
    struct SwsContext *sub_convert_ctx; // 字幕尺⼨格式变换
    int eof;                            // =0未读取结束, =1读取结束

    char *filename;                     // ⽂件名
    int width, height, xleft, ytop;     // 窗口宽、⾼，x起始坐标，y起始坐标
    int step;                           // =1 步进播放模式, =0 其他模式

#if CONFIG_AVFILTER
    int vfilter_idx;
    AVFilterContext *in_video_filter;   // the first filter in the video chain
    AVFilterContext *out_video_filter;  // the last filter in the video chain
    AVFilterContext *in_audio_filter;   // the first filter in the audio chain
    AVFilterContext *out_audio_filter;  // the last filter in the audio chain
    AVFilterGraph *agraph;              // audio filter graph
#endif

    // 保留最近的相应audio、video、subtitle流的steam index
    int last_video_stream, last_audio_stream, last_subtitle_stream;

    SDL_cond *continue_read_thread;     // 当读取数据队列满了后进⼊休眠时，可以通过该condition唤醒读线程
} VideoState;

#endif /* FF_TYPES_H */

