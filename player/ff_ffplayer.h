/*
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2013-2015 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef FF_FFPLAYER_H
#define FF_FFPLAYER_H

#include "ff_types.h"

/*
 * Option categories for ffp_set_option / ffp_set_option_int
 */
#define FFP_OPT_CATEGORY_FORMAT   1   /* AVFormatContext options */
#define FFP_OPT_CATEGORY_CODEC    2   /* AVCodecContext options */
#define FFP_OPT_CATEGORY_SWS      3   /* SwsContext options */
#define FFP_OPT_CATEGORY_PLAYER   4   /* FFPlayer options */
#define FFP_OPT_CATEGORY_SWR      5   /* SwrContext options */

/*
 * Property IDs - 已移至 ff_ffmsg.h，避免重复定义
 * 使用 ff_ffmsg.h 中的定义：
 *   FFP_PROP_FLOAT_PLAYBACK_RATE, FFP_PROP_FLOAT_PLAYBACK_VOLUME
 *   FFP_PROP_INT64_VIDEO_CACHED_BYTES, FFP_PROP_INT64_AUDIO_CACHED_BYTES
 *   FFP_PROP_INT64_VIDEO_CACHED_PACKETS, FFP_PROP_INT64_AUDIO_CACHED_PACKETS
 */
#include "ff_ffmsg.h"

/*
 * =============================================================================
 * 渲染模式和回调定义
 * =============================================================================
 */

/**
 * 渲染模式
 */
typedef enum FFPRenderMode {
    FFP_RENDER_MODE_SDL = 0,       /* SDL 子窗口渲染（默认）*/
    FFP_RENDER_MODE_CALLBACK = 1,  /* 回调模式，外部渲染 */
} FFPRenderMode;

/*
 * =============================================================================
 * 硬件加速定义
 * =============================================================================
 */

/**
 * 硬件加速类型枚举
 */
typedef enum FFPHWAccelType {
    FFP_HWACCEL_NONE = 0,          /* 软解码（默认）*/
    FFP_HWACCEL_AUTO,              /* 自动选择最佳硬件加速 */
    FFP_HWACCEL_DXVA2,             /* Windows DXVA2 */
    FFP_HWACCEL_D3D11VA,           /* Windows D3D11 Video Acceleration */
    FFP_HWACCEL_CUDA,              /* NVIDIA CUDA/NVDEC */
    FFP_HWACCEL_VAAPI,             /* Linux VAAPI */
    FFP_HWACCEL_VDPAU,             /* Linux VDPAU */
    FFP_HWACCEL_VIDEOTOOLBOX,      /* macOS VideoToolbox */
    FFP_HWACCEL_QSV,               /* Intel Quick Sync Video */
    FFP_HWACCEL_COUNT              /* 类型总数 */
} FFPHWAccelType;

/**
 * 硬件加速信息结构
 */
typedef struct FFPHWAccelInfo {
    FFPHWAccelType type;           /* 硬件加速类型 */
    const char *name;              /* 名称 */
    const char *description;       /* 描述 */
    int available;                 /* 是否可用 */
} FFPHWAccelInfo;

/**
 * 传递给外部的视频帧数据
 */
typedef struct FFPVideoFrame {
    uint8_t *data[4];      /* YUV/RGB 数据平面 */
    int linesize[4];       /* 每行字节数 */
    int width;             /* 帧宽度 */
    int height;            /* 帧高度 */
    int format;            /* 像素格式 (AVPixelFormat) */
    double pts;            /* 显示时间戳（秒）*/
    int64_t pos;           /* 文件位置 */
} FFPVideoFrame;

/**
 * 视频帧回调函数类型
 * @param opaque 用户数据指针
 * @param frame 视频帧数据（在回调返回后可能失效，需立即复制）
 */
typedef void (*ffp_video_frame_callback)(void *opaque, FFPVideoFrame *frame);

/**
 * FFPlayer - 播放器实例结构体
 * 
 * 参考 ijkplayer 的 FFPlayer 设计，将原 ffplay.c 中的全局变量封装为实例变量，
 * 以支持多实例播放。
 */
typedef struct FFPlayer {
    /* 核心播放状态 */
    VideoState *is;

    /* 输入源 */
    char *input_filename;
    const AVInputFormat *iformat;

    /* 流选项 */
    int audio_disable;
    int video_disable;
    int subtitle_disable;
    const char *wanted_stream_spec[AVMEDIA_TYPE_NB];

    /* 同步与播放控制 */
    int av_sync_type;
    int64_t start_time;
    int64_t duration;
    int loop;
    int autoexit;
    int framedrop;
    int infinite_buffer;
    int seek_by_bytes;
    float seek_interval;

    /* 解码选项 */
    int fast;
    int genpts;
    int lowres;
    int decoder_reorder_pts;
    int find_stream_info;

    /* 编解码器指定 */
    const char *audio_codec_name;
    const char *video_codec_name;
    const char *subtitle_codec_name;

    /* 滤镜选项 */
#if CONFIG_AVFILTER
    const char **vfilters_list;
    int nb_vfilters;
    char *afilters;
    int filter_nbthreads;
#endif
    int autorotate;

    /* 显示选项 */
    int default_width;
    int default_height;
    int screen_width;
    int screen_height;
    int display_disable;
    enum ShowMode show_mode;
    double rdftspeed;
    int show_status;

    /* 音频选项 */
    int startup_volume;

    /* SDL 窗口和音频 */
    SDL_Window *window;
    void *native_window;
    int use_external_window;    /* 已废弃，总是 0（只使用子窗口模式）*/
    SDL_AudioDeviceID audio_dev;

    /* 视频输出 (OpenGL) */
    struct FFVout *vout;
    
    /* 渲染线程 */
    SDL_Thread *render_tid;
    int render_thread_running;
    int auto_render_enabled;

    /* 运行时状态 */
    int is_full_screen;
    int64_t audio_callback_time;
    unsigned sws_flags;

    /* 窗口选项 */
    const char *window_title;
    int screen_left;
    int screen_top;
    int borderless;
    int alwaysontop;
    int exit_on_keydown;
    int exit_on_mousedown;
    int64_t cursor_last_shown;
    int cursor_hidden;

    /* 额外状态字段 (参考 ijkplayer) */
    int prepared;
    int error;
    int last_error;
    int auto_resume;
    int start_on_prepared;
    int64_t playable_duration_ms;
    int packet_buffering;
    int max_fps;

    /* 播放速率 */
    float playback_rate;
    int playback_rate_changed;  /* 倍速变化标志，用于动态重配置音频滤镜 */

    /* 硬件加速 */
    FFPHWAccelType hwaccel_type;       /* 当前硬件加速类型 */
    char *hwaccel_device;              /* 硬件设备路径（可选）*/
    void *hw_device_ctx;               /* AVBufferRef *hw_device_ctx 硬件设备上下文 */
    int hw_pix_fmt;                    /* enum AVPixelFormat 硬件像素格式 */
    int hwaccel_failed;                /* 硬解失败标志，回退软解 */
    int hwaccel_retrieve_data;         /* 是否需要从 GPU 拷贝数据到 CPU */

    /* 选项字典 (参考 ijkplayer) */
    AVDictionary *format_opts;
    AVDictionary *codec_opts;
    AVDictionary *sws_dict;
    AVDictionary *swr_opts;

    /* 外部消息队列（指向 MediaPlayer 的 msg_queue）*/
    /* 用于 FFPlayer 层向 MediaPlayer 层发送事件通知 */
    struct MessageQueue *ext_msg_queue;

    /* 渲染模式 */
    FFPRenderMode render_mode;
    
    /* 回调渲染相关 */
    ffp_video_frame_callback video_frame_cb;
    void *video_frame_cb_opaque;

} FFPlayer;

/*
 * =============================================================================
 * 全局初始化/反初始化
 * =============================================================================
 */

void ffp_global_init(void);
void ffp_global_uninit(void);

/*
 * =============================================================================
 * FFPlayer 生命周期管理
 * =============================================================================
 */
FFPlayer *ffp_create(void);
void ffp_destroy(FFPlayer *ffp);
void ffp_reset(FFPlayer *ffp);
void ffp_set_defaults(FFPlayer *ffp);

/*
 * =============================================================================
 * SDL 初始化与窗口管理
 * =============================================================================
 */

/**
 * 初始化 SDL
 * @return 0 成功，负数 失败
 */
int ffp_init_sdl(FFPlayer *ffp);

/**
 * 创建窗口和渲染器
 * @return 0 成功，负数 失败
 */
int ffp_create_window(FFPlayer *ffp);

/**
 * 设置原生窗口句柄 (必须在 ffp_create_window 之前调用)
 * @param handle 原生窗口句柄 (HWND / NSWindow* / Window)
 * @return 0 成功，负数 失败
 */
int ffp_set_window_handle(FFPlayer *ffp, void *handle);

/**
 * 设置默认窗口大小 (必须在 ffp_create_window 之前调用)
 * @param width 宽度
 * @param height 高度
 */
void ffp_set_default_window_size(FFPlayer *ffp, int width, int height);

/**
 * 获取 SDL 窗口指针 (用于平台相关的嵌入操作)
 * @return SDL_Window 指针
 */
SDL_Window *ffp_get_sdl_window(FFPlayer *ffp);

/**
 * 一键附加窗口并初始化 (简化 Qt 端调用)
 * @param parent_handle 父窗口句柄
 * @param width 窗口宽度
 * @param height 窗口高度
 * @return 0 成功，负数 失败
 */
int ffp_attach_window(FFPlayer *ffp, void *parent_handle, int width, int height);

/**
 * 启动自动渲染线程
 * @return 0 成功，负数 失败
 */
int ffp_start_render_thread(FFPlayer *ffp);

/**
 * 停止自动渲染线程
 */
void ffp_stop_render_thread(FFPlayer *ffp);

/**
 * 关闭播放器并清理资源
 */
void ffp_shutdown(FFPlayer *ffp);

/*
 * =============================================================================
 * 播放控制
 * =============================================================================
 */

/**
 * 异步准备播放（打开媒体源）
 * @param ffp FFPlayer实例
 * @param file_name 媒体文件路径或URL
 * @return 0 成功，负数 失败
 */
int ffp_prepare_async(FFPlayer *ffp, const char *file_name);

/**
 * 开始/恢复播放
 * @return 0 成功，负数 失败
 */
int ffp_start(FFPlayer *ffp);

/**
 * 暂停播放
 * @return 0 成功，负数 失败
 */
int ffp_pause(FFPlayer *ffp);

/**
 * 停止播放
 * @return 0 成功，负数 失败
 */
int ffp_stop(FFPlayer *ffp);

/**
 * 跳转到指定位置
 * @param msec 目标位置（毫秒）
 * @return 0 成功，负数 失败
 */
int ffp_seek_to(FFPlayer *ffp, long msec);

/*
 * =============================================================================
 * 渲染控制
 * =============================================================================
 */

/**
 * 渲染一帧 (由渲染线程调用)
 * @return 下一帧的等待时间（秒），用于精确帧率控制
 */
double ffp_render_frame(FFPlayer *ffp);

/*
 * =============================================================================
 * 播放控制（封装 ffplay 函数）
 * =============================================================================
 */

/**
 * 切换暂停/播放状态
 */
void ffp_toggle_pause(FFPlayer *ffp);

/**
 * 切换静音
 */
void ffp_toggle_mute(FFPlayer *ffp);

/**
 * 切换全屏
 */
void ffp_toggle_full_screen(FFPlayer *ffp);

/**
 * 切换音频显示模式
 */
void ffp_toggle_audio_display(FFPlayer *ffp);

/**
 * 调节音量
 * @param sign 方向 (1=增加, -1=减少)
 * @param step 步长
 */
void ffp_update_volume(FFPlayer *ffp, int sign, double step);

/**
 * 单帧步进
 */
void ffp_step_to_next_frame(FFPlayer *ffp);

/**
 * 切换流（音频/视频/字幕）
 * @param codec_type 流类型
 */
void ffp_stream_cycle_channel(FFPlayer *ffp, int codec_type);

/*
 * =============================================================================
 * 跳转控制
 * =============================================================================
 */

/**
 * 相对跳转
 * @param incr 跳转增量（秒）
 */
void ffp_seek_relative(FFPlayer *ffp, double incr);

/**
 * 章节跳转
 * @param incr 方向 (1=下一章, -1=上一章)
 */
void ffp_seek_chapter(FFPlayer *ffp, int incr);

/**
 * 按百分比跳转
 * @param frac 位置百分比 (0.0-1.0)
 */
void ffp_seek_to_percent(FFPlayer *ffp, double frac);

/*
 * =============================================================================
 * 状态查询
 * =============================================================================
 */

/**
 * 获取当前播放位置
 * @return 当前位置（毫秒），失败返回0
 */
long ffp_get_current_position(FFPlayer *ffp);

/**
 * 获取媒体总时长
 * @return 总时长（毫秒），失败返回0
 */
long ffp_get_duration(FFPlayer *ffp);

/**
 * 获取可播放时长（已缓冲）
 * @return 可播放时长（毫秒），当前空实现返回0
 */
long ffp_get_playable_duration(FFPlayer *ffp);

/**
 * 是否暂停状态
 */
int ffp_is_paused(FFPlayer *ffp);

/**
 * 是否正在播放
 */
int ffp_is_playing(FFPlayer *ffp);

/*
 * =============================================================================
 * 音量控制
 * =============================================================================
 */

/**
 * 设置音量
 * @param volume 音量值 (0.0 - 1.0)
 */
void ffp_set_volume(FFPlayer *ffp, float volume);

/**
 * 获取当前音量
 * @return 音量值 (0.0 - 1.0)
 */
float ffp_get_volume(FFPlayer *ffp);

/**
 * 设置静音
 * @param mute 1=静音, 0=取消静音
 */
void ffp_set_mute(FFPlayer *ffp, int mute);

/**
 * 是否静音
 * @return 1=静音, 0=非静音
 */
int ffp_is_muted(FFPlayer *ffp);

/*
 * =============================================================================
 * 循环控制
 * =============================================================================
 */

/**
 * 设置循环次数
 * @param loop 循环次数 (0=无限循环, 1=不循环, >1=循环次数)
 */
void ffp_set_loop(FFPlayer *ffp, int loop);

/**
 * 获取循环次数
 */
int ffp_get_loop(FFPlayer *ffp);

/*
 * =============================================================================
 * 流切换和编解码器信息
 * =============================================================================
 */

/**
 * 切换流（音频/视频/字幕）
 * @param stream_type 流类型 (AVMEDIA_TYPE_AUDIO/VIDEO/SUBTITLE)
 * @return 0 成功，负数 失败
 */
int ffp_set_stream_selected(FFPlayer *ffp, int stream_type);

/**
 * 获取视频编解码器信息
 * @param codec_info 输出编解码器信息字符串（需要调用者free）
 * @return 0 成功，负数 失败
 */
int ffp_get_video_codec_info(FFPlayer *ffp, char **codec_info);

/**
 * 获取音频编解码器信息
 * @param codec_info 输出编解码器信息字符串（需要调用者free）
 * @return 0 成功，负数 失败
 */
int ffp_get_audio_codec_info(FFPlayer *ffp, char **codec_info);

/*
 * =============================================================================
 * 选项设置
 * =============================================================================
 */

/**
 * 设置字符串选项
 * @param opt_category 选项类别 (FFP_OPT_CATEGORY_*)
 * @param name 选项名
 * @param value 选项值
 */
void ffp_set_option(FFPlayer *ffp, int opt_category, const char *name, const char *value);

/**
 * 设置整数选项
 * @param opt_category 选项类别 (FFP_OPT_CATEGORY_*)
 * @param name 选项名
 * @param value 选项值
 */
void ffp_set_option_int(FFPlayer *ffp, int opt_category, const char *name, int64_t value);

/*
 * =============================================================================
 * 属性访问
 * =============================================================================
 */

/**
 * 获取浮点属性
 * @param id 属性ID (FFP_PROP_FLOAT_*)
 * @param default_value 默认值
 * @return 属性值
 */
float ffp_get_property_float(FFPlayer *ffp, int id, float default_value);

/**
 * 设置浮点属性
 * @param id 属性ID (FFP_PROP_FLOAT_*)
 * @param value 属性值
 */
void ffp_set_property_float(FFPlayer *ffp, int id, float value);

/**
 * 获取整数属性
 * @param id 属性ID (FFP_PROP_INT64_*)
 * @param default_value 默认值
 * @return 属性值
 */
int64_t ffp_get_property_int64(FFPlayer *ffp, int id, int64_t default_value);

/**
 * 设置整数属性
 * @param id 属性ID (FFP_PROP_INT64_*)
 * @param value 属性值
 */
void ffp_set_property_int64(FFPlayer *ffp, int id, int64_t value);

/*
 * =============================================================================
 * 可选/后续实现的函数（当前空实现）
 * =============================================================================
 */

/**
 * 设置播放速率
 * @param rate 播放速率 (1.0=正常, <1.0=慢放, >1.0=快放)
 * @note 当前空实现，ffplay不支持变速播放
 */
void ffp_set_playback_rate(FFPlayer *ffp, float rate);

/**
 * 获取视频旋转角度
 * @return 旋转角度（度），当前空实现返回0
 */
int ffp_get_video_rotate_degrees(FFPlayer *ffp);

/*
 * =============================================================================
 * 渲染模式设置
 * =============================================================================
 */

/**
 * 设置渲染模式
 * @param ffp FFPlayer实例
 * @param mode 渲染模式 (FFP_RENDER_MODE_SDL 或 FFP_RENDER_MODE_CALLBACK)
 */
void ffp_set_render_mode(FFPlayer *ffp, FFPRenderMode mode);

/**
 * 获取渲染模式
 * @param ffp FFPlayer实例
 * @return 当前渲染模式
 */
FFPRenderMode ffp_get_render_mode(FFPlayer *ffp);

/**
 * 设置视频帧回调函数（用于 FFP_RENDER_MODE_CALLBACK 模式）
 * @param ffp FFPlayer实例
 * @param cb 回调函数
 * @param opaque 传递给回调的用户数据
 */
void ffp_set_video_frame_callback(FFPlayer *ffp, ffp_video_frame_callback cb, void *opaque);

/*
 * =============================================================================
 * 硬件加速控制
 * =============================================================================
 */

/**
 * 设置硬件加速类型（必须在 ffp_prepare_async 之前调用）
 * @param ffp FFPlayer实例
 * @param type 硬件加速类型
 */
void ffp_set_hwaccel_type(FFPlayer *ffp, FFPHWAccelType type);

/**
 * 获取当前硬件加速类型
 * @param ffp FFPlayer实例
 * @return 硬件加速类型
 */
FFPHWAccelType ffp_get_hwaccel_type(FFPlayer *ffp);

/**
 * 获取可用的硬件加速列表
 * @param infos 输出信息数组（调用者提供，大小至少为 FFP_HWACCEL_COUNT）
 * @param max_count 数组最大容量
 * @return 实际可用的硬件加速数量
 */
int ffp_get_available_hwaccels(FFPHWAccelInfo *infos, int max_count);

/**
 * 检查指定硬件加速是否可用
 * @param type 硬件加速类型
 * @return 1=可用, 0=不可用
 */
int ffp_is_hwaccel_available(FFPHWAccelType type);

/**
 * 获取硬件加速类型名称
 * @param type 硬件加速类型
 * @return 名称字符串
 */
const char *ffp_get_hwaccel_name(FFPHWAccelType type);

#endif /* FF_FFPLAYER_H */
