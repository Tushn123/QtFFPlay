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

#include "ff_ffplayer.h"

void ffp_set_defaults(FFPlayer *ffp)
{
    /* 输入源 */
    ffp->input_filename = NULL;
    ffp->iformat = NULL;

    /* 流选项 */
    ffp->audio_disable = 0;
    ffp->video_disable = 0;
    ffp->subtitle_disable = 0;
    memset(ffp->wanted_stream_spec, 0, sizeof(ffp->wanted_stream_spec));

    /* 同步与播放控制 */
    ffp->av_sync_type = AV_SYNC_AUDIO_MASTER;
    ffp->start_time = AV_NOPTS_VALUE;
    ffp->duration = AV_NOPTS_VALUE;
    ffp->loop = 1;
    ffp->autoexit = 0;
    ffp->framedrop = -1;
    ffp->infinite_buffer = -1;
    ffp->seek_by_bytes = -1;
    ffp->seek_interval = 10;

    /* 解码选项 */
    ffp->fast = 0;
    ffp->genpts = 0;
    ffp->lowres = 0;
    ffp->decoder_reorder_pts = -1;
    ffp->find_stream_info = 1;

    /* 编解码器指定 */
    ffp->audio_codec_name = NULL;
    ffp->video_codec_name = NULL;
    ffp->subtitle_codec_name = NULL;

    /* 滤镜选项 */
#if CONFIG_AVFILTER
    ffp->vfilters_list = NULL;
    ffp->nb_vfilters = 0;
    ffp->afilters = NULL;
    ffp->filter_nbthreads = 0;
#endif
    ffp->autorotate = 1;

    /* 显示选项 */
    ffp->default_width = 640;
    ffp->default_height = 480;
    ffp->screen_width = 0;
    ffp->screen_height = 0;
    ffp->display_disable = 0;
    ffp->show_mode = SHOW_MODE_NONE;
    ffp->rdftspeed = 0.02;
    ffp->show_status = -1;

    /* 音频选项 */
    ffp->startup_volume = 100;

    /* SDL 渲染相关 */
    ffp->window = NULL;
    ffp->renderer = NULL;
    memset(&ffp->renderer_info, 0, sizeof(ffp->renderer_info));
    ffp->audio_dev = 0;

    /* 运行时状态 */
    ffp->is_full_screen = 0;
    ffp->audio_callback_time = 0;
    ffp->sws_flags = SWS_BICUBIC;

    /* 窗口选项 */
    ffp->window_title = NULL;
    ffp->screen_left = SDL_WINDOWPOS_CENTERED;
    ffp->screen_top = SDL_WINDOWPOS_CENTERED;
    ffp->borderless = 0;
    ffp->alwaysontop = 0;
    ffp->exit_on_keydown = 0;
    ffp->exit_on_mousedown = 0;
    ffp->cursor_last_shown = 0;
    ffp->cursor_hidden = 0;

    /* 额外状态字段 */
    ffp->prepared = 0;
    ffp->error = 0;
    ffp->last_error = 0;
    ffp->auto_resume = 0;
    ffp->start_on_prepared = 1;
    ffp->playable_duration_ms = 0;
    ffp->packet_buffering = 1;
    ffp->max_fps = 31;

    /* 选项字典 */
    ffp->format_opts = NULL;
    ffp->codec_opts = NULL;
    ffp->sws_dict = NULL;
    ffp->swr_opts = NULL;
}

FFPlayer *ffp_create(void)
{
    FFPlayer *ffp = av_mallocz(sizeof(FFPlayer));
    if (!ffp)
        return NULL;

    ffp->is = NULL;
    ffp_set_defaults(ffp);

    return ffp;
}

void ffp_reset(FFPlayer *ffp)
{
    if (!ffp)
        return;

    /* 释放选项字典 */
    av_dict_free(&ffp->format_opts);
    av_dict_free(&ffp->codec_opts);
    av_dict_free(&ffp->sws_dict);
    av_dict_free(&ffp->swr_opts);

    /* 释放字符串 */
    av_freep(&ffp->input_filename);
#if CONFIG_AVFILTER
    av_freep(&ffp->vfilters_list);
#endif

    /* 重置为默认值 */
    ffp_set_defaults(ffp);
}

void ffp_destroy(FFPlayer *ffp)
{
    if (!ffp)
        return;

    ffp_reset(ffp);
    av_free(ffp);
}

