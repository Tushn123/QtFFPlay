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

    /* 选项字典 (参考 ijkplayer) */
    AVDictionary *format_opts;
    AVDictionary *codec_opts;
    AVDictionary *sws_dict;
    AVDictionary *swr_opts;

} FFPlayer;

/* FFPlayer 生命周期管理 */
FFPlayer *ffp_create(void);
void ffp_destroy(FFPlayer *ffp);
void ffp_reset(FFPlayer *ffp);

/* FFPlayer 默认值设置 */
void ffp_set_defaults(FFPlayer *ffp);

#endif /* FF_FFPLAYER_H */

