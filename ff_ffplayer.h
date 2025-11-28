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
 * Property IDs for ffp_get_property_* / ffp_set_property_*
 */
/* Float properties */
#define FFP_PROP_FLOAT_PLAYBACK_RATE            0
#define FFP_PROP_FLOAT_PLAYBACK_VOLUME          1

/* Int64 properties */
#define FFP_PROP_INT64_CURRENT_POSITION         1000
#define FFP_PROP_INT64_DURATION                 1001
#define FFP_PROP_INT64_VIDEO_CACHED_BYTES       1002
#define FFP_PROP_INT64_AUDIO_CACHED_BYTES       1003
#define FFP_PROP_INT64_VIDEO_CACHED_PACKETS     1004
#define FFP_PROP_INT64_AUDIO_CACHED_PACKETS     1005

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

    /* SDL 渲染相关 */
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_RendererInfo renderer_info;
    SDL_AudioDeviceID audio_dev;

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

    /* 播放速率 (当前空实现，ffplay不支持) */
    float playback_rate;

    /* 选项字典 (参考 ijkplayer) */
    AVDictionary *format_opts;
    AVDictionary *codec_opts;
    AVDictionary *sws_dict;
    AVDictionary *swr_opts;

} FFPlayer;

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
 * 事件循环
 * =============================================================================
 */

/**
 * 主事件循环
 * 处理用户输入、视频刷新等事件
 */
void ffp_event_loop(FFPlayer *ffp);

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

#endif /* FF_FFPLAYER_H */
