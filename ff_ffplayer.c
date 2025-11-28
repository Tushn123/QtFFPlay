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
#include "ffplay.h"
#include "cmdutils.h"

/*
 * =============================================================================
 * FFPlayer 生命周期管理
 * =============================================================================
 */

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

    /* 播放速率 */
    ffp->playback_rate = 1.0f;

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

/*
 * =============================================================================
 * SDL 初始化与窗口管理
 * =============================================================================
 */

int ffp_init_sdl(FFPlayer *ffp)
{
    int flags;

    if (!ffp)
        return -1;

    if (ffp->display_disable) {
        ffp->video_disable = 1;
    }

    flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (ffp->audio_disable)
        flags &= ~SDL_INIT_AUDIO;
    else {
        /* Try to work around an occasional ALSA buffer underflow issue when the
         * period size is NPOT due to ALSA resampling by forcing the buffer size. */
        if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
            SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);
    }
    if (ffp->display_disable)
        flags &= ~SDL_INIT_VIDEO;

    if (SDL_Init(flags)) {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        return -1;
    }

    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

    return 0;
}

int ffp_create_window(FFPlayer *ffp)
{
    if (!ffp)
        return -1;

    if (ffp->display_disable)
        return 0;

    int flags = SDL_WINDOW_HIDDEN;
    if (ffp->alwaysontop)
#if SDL_VERSION_ATLEAST(2,0,5)
        flags |= SDL_WINDOW_ALWAYS_ON_TOP;
#else
        av_log(NULL, AV_LOG_WARNING, "Your SDL version doesn't support SDL_WINDOW_ALWAYS_ON_TOP. Feature will be inactive.\n");
#endif
    if (ffp->borderless)
        flags |= SDL_WINDOW_BORDERLESS;
    else
        flags |= SDL_WINDOW_RESIZABLE;

#ifdef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif

    ffp->window = SDL_CreateWindow(program_name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                   ffp->default_width, ffp->default_height, flags);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    if (ffp->window) {
        ffp->renderer = SDL_CreateRenderer(ffp->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ffp->renderer) {
            av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
            ffp->renderer = SDL_CreateRenderer(ffp->window, -1, 0);
        }
        if (ffp->renderer) {
            if (!SDL_GetRendererInfo(ffp->renderer, &ffp->renderer_info))
                av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", ffp->renderer_info.name);
        }
    }

    if (!ffp->window || !ffp->renderer || !ffp->renderer_info.num_texture_formats) {
        av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
        return -1;
    }

    return 0;
}

void ffp_shutdown(FFPlayer *ffp)
{
    if (!ffp)
        return;

    if (ffp->is) {
        stream_close(ffp, ffp->is);
        ffp->is = NULL;
    }

    if (ffp->renderer)
        SDL_DestroyRenderer(ffp->renderer);
    if (ffp->window)
        SDL_DestroyWindow(ffp->window);

    ffp->renderer = NULL;
    ffp->window = NULL;

    uninit_opts();
#if CONFIG_AVFILTER
    av_freep(&ffp->vfilters_list);
#endif
    avformat_network_deinit();
    if (ffp->show_status)
        printf("\n");
    SDL_Quit();
    ffp_destroy(ffp);
    av_log(NULL, AV_LOG_QUIET, "%s", "");
}

/*
 * =============================================================================
 * 事件循环
 * =============================================================================
 */

void ffp_event_loop(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return;

    do_event_loop(ffp);
}

/*
 * =============================================================================
 * 播放控制（封装 ffplay 函数）
 * =============================================================================
 */

void ffp_toggle_pause(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return;

    toggle_pause(ffp->is);
}

void ffp_toggle_mute(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return;

    toggle_mute(ffp->is);
}

void ffp_toggle_full_screen(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return;

    toggle_full_screen(ffp, ffp->is);
    ffp->is->force_refresh = 1;
}

void ffp_toggle_audio_display(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return;

    toggle_audio_display(ffp->is);
}

void ffp_update_volume(FFPlayer *ffp, int sign, double step)
{
    if (!ffp || !ffp->is)
        return;

    update_volume(ffp->is, sign, step);
}

void ffp_step_to_next_frame(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return;

    step_to_next_frame(ffp->is);
}

void ffp_stream_cycle_channel(FFPlayer *ffp, int codec_type)
{
    if (!ffp || !ffp->is)
        return;

    stream_cycle_channel(ffp, ffp->is, codec_type);
}

/*
 * =============================================================================
 * 跳转控制
 * =============================================================================
 */

void ffp_seek_relative(FFPlayer *ffp, double incr)
{
    if (!ffp || !ffp->is)
        return;

    VideoState *is = ffp->is;
    double pos;

    if (ffp->seek_by_bytes) {
        pos = -1;
        if (pos < 0 && is->video_stream >= 0)
            pos = frame_queue_last_pos(&is->pictq);
        if (pos < 0 && is->audio_stream >= 0)
            pos = frame_queue_last_pos(&is->sampq);
        if (pos < 0)
            pos = avio_tell(is->ic->pb);
        if (is->ic->bit_rate)
            incr *= is->ic->bit_rate / 8.0;
        else
            incr *= 180000.0;
        pos += incr;
        stream_seek(is, pos, incr, 1);
    } else {
        pos = get_master_clock(is);
        if (isnan(pos))
            pos = (double)is->seek_pos / AV_TIME_BASE;
        pos += incr;
        if (is->ic->start_time != AV_NOPTS_VALUE && pos < is->ic->start_time / (double)AV_TIME_BASE)
            pos = is->ic->start_time / (double)AV_TIME_BASE;
        stream_seek(is, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
    }
}

void ffp_seek_chapter(FFPlayer *ffp, int incr)
{
    if (!ffp || !ffp->is)
        return;

    seek_chapter(ffp, incr);
}

void ffp_seek_to_percent(FFPlayer *ffp, double frac)
{
    if (!ffp || !ffp->is || !ffp->is->ic)
        return;

    VideoState *is = ffp->is;

    if (ffp->seek_by_bytes || is->ic->duration <= 0) {
        uint64_t size = avio_size(is->ic->pb);
        stream_seek(is, size * frac, 0, 1);
    } else {
        int64_t ts;
        int ns, hh, mm, ss;
        int tns, thh, tmm, tss;
        tns  = is->ic->duration / 1000000LL;
        thh  = tns / 3600;
        tmm  = (tns % 3600) / 60;
        tss  = (tns % 60);
        ns   = frac * tns;
        hh   = ns / 3600;
        mm   = (ns % 3600) / 60;
        ss   = (ns % 60);
        av_log(NULL, AV_LOG_INFO,
               "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
                hh, mm, ss, thh, tmm, tss);
        ts = frac * is->ic->duration;
        if (is->ic->start_time != AV_NOPTS_VALUE)
            ts += is->ic->start_time;
        stream_seek(is, ts, 0, 0);
    }
}

/*
 * =============================================================================
 * 播放控制
 * =============================================================================
 */

int ffp_prepare_async(FFPlayer *ffp, const char *file_name)
{
    if (!ffp || !file_name)
        return -1;

    /* 保存文件名 */
    av_freep(&ffp->input_filename);
    ffp->input_filename = av_strdup(file_name);
    if (!ffp->input_filename)
        return -1;

    /* 调用 stream_open 创建 VideoState */
    ffp->is = stream_open(ffp, ffp->input_filename, ffp->iformat);
    if (!ffp->is) {
        av_log(NULL, AV_LOG_ERROR, "ffp_prepare_async: stream_open failed\n");
        return -1;
    }

    ffp->prepared = 1;
    return 0;
}

int ffp_start(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return -1;

    if (ffp->is->paused) {
        toggle_pause(ffp->is);
    }
    return 0;
}

int ffp_pause(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return -1;

    if (!ffp->is->paused) {
        toggle_pause(ffp->is);
    }
    return 0;
}

int ffp_stop(FFPlayer *ffp)
{
    if (!ffp)
        return -1;

    if (ffp->is) {
        stream_close(ffp, ffp->is);
        ffp->is = NULL;
    }
    ffp->prepared = 0;
    return 0;
}

int ffp_seek_to(FFPlayer *ffp, long msec)
{
    if (!ffp || !ffp->is)
        return -1;

    int64_t pos = (int64_t)msec * 1000; /* 转换为微秒 */
    int64_t rel = 0;

    if (ffp->is->ic && ffp->is->ic->start_time != AV_NOPTS_VALUE)
        pos += ffp->is->ic->start_time;

    stream_seek(ffp->is, pos, rel, 0);
    return 0;
}

/*
 * =============================================================================
 * 状态查询
 * =============================================================================
 */

long ffp_get_current_position(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return 0;

    double pos = get_master_clock(ffp->is);
    if (isnan(pos))
        pos = (double)ffp->is->seek_pos / AV_TIME_BASE;

    /* 转换为毫秒 */
    return (long)(pos * 1000);
}

long ffp_get_duration(FFPlayer *ffp)
{
    if (!ffp || !ffp->is || !ffp->is->ic)
        return 0;

    int64_t duration = ffp->is->ic->duration;
    if (duration == AV_NOPTS_VALUE)
        return 0;

    /* 转换为毫秒 */
    return (long)(duration / 1000);
}

long ffp_get_playable_duration(FFPlayer *ffp)
{
    /* 当前空实现，后续可以通过计算缓冲区来实现 */
    (void)ffp;
    return 0;
}

int ffp_is_paused(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return 1; /* 如果没有播放实例，认为是暂停状态 */

    return ffp->is->paused;
}

int ffp_is_playing(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return 0;

    return !ffp->is->paused && !ffp->is->abort_request;
}

/*
 * =============================================================================
 * 音量控制
 * =============================================================================
 */

void ffp_set_volume(FFPlayer *ffp, float volume)
{
    if (!ffp || !ffp->is)
        return;

    /* 限制范围 0.0 - 1.0 */
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    /* 转换为 SDL 音量 (0 - SDL_MIX_MAXVOLUME) */
    ffp->is->audio_volume = (int)(volume * SDL_MIX_MAXVOLUME);
}

float ffp_get_volume(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return 0.0f;

    return (float)ffp->is->audio_volume / SDL_MIX_MAXVOLUME;
}

void ffp_set_mute(FFPlayer *ffp, int mute)
{
    if (!ffp || !ffp->is)
        return;

    ffp->is->muted = mute ? 1 : 0;
}

int ffp_is_muted(FFPlayer *ffp)
{
    if (!ffp || !ffp->is)
        return 0;

    return ffp->is->muted;
}

/*
 * =============================================================================
 * 循环控制
 * =============================================================================
 */

void ffp_set_loop(FFPlayer *ffp, int loop)
{
    if (!ffp)
        return;

    ffp->loop = loop;
}

int ffp_get_loop(FFPlayer *ffp)
{
    if (!ffp)
        return 0;

    return ffp->loop;
}

/*
 * =============================================================================
 * 流切换和编解码器信息
 * =============================================================================
 */

int ffp_set_stream_selected(FFPlayer *ffp, int stream_type)
{
    if (!ffp || !ffp->is)
        return -1;

    stream_cycle_channel(ffp, ffp->is, stream_type);
    return 0;
}

int ffp_get_video_codec_info(FFPlayer *ffp, char **codec_info)
{
    if (!ffp || !ffp->is || !codec_info)
        return -1;

    *codec_info = NULL;

    if (ffp->is->video_st && ffp->is->video_st->codecpar) {
        const AVCodecDescriptor *desc = avcodec_descriptor_get(ffp->is->video_st->codecpar->codec_id);
        if (desc) {
            *codec_info = av_asprintf("%s, %dx%d",
                desc->name,
                ffp->is->video_st->codecpar->width,
                ffp->is->video_st->codecpar->height);
            return 0;
        }
    }
    return -1;
}

int ffp_get_audio_codec_info(FFPlayer *ffp, char **codec_info)
{
    if (!ffp || !ffp->is || !codec_info)
        return -1;

    *codec_info = NULL;

    if (ffp->is->audio_st && ffp->is->audio_st->codecpar) {
        const AVCodecDescriptor *desc = avcodec_descriptor_get(ffp->is->audio_st->codecpar->codec_id);
        if (desc) {
            *codec_info = av_asprintf("%s, %d Hz, %d channels",
                desc->name,
                ffp->is->audio_st->codecpar->sample_rate,
                ffp->is->audio_st->codecpar->ch_layout.nb_channels);
            return 0;
        }
    }
    return -1;
}

/*
 * =============================================================================
 * 选项设置
 * =============================================================================
 */

void ffp_set_option(FFPlayer *ffp, int opt_category, const char *name, const char *value)
{
    if (!ffp || !name)
        return;

    AVDictionary **dict = NULL;

    switch (opt_category) {
    case FFP_OPT_CATEGORY_FORMAT:
        dict = &ffp->format_opts;
        break;
    case FFP_OPT_CATEGORY_CODEC:
        dict = &ffp->codec_opts;
        break;
    case FFP_OPT_CATEGORY_SWS:
        dict = &ffp->sws_dict;
        break;
    case FFP_OPT_CATEGORY_SWR:
        dict = &ffp->swr_opts;
        break;
    case FFP_OPT_CATEGORY_PLAYER:
        /* 播放器选项直接设置到 FFPlayer 结构体 */
        /* 这里可以添加具体的选项处理 */
        return;
    default:
        return;
    }

    if (dict) {
        av_dict_set(dict, name, value, 0);
    }
}

void ffp_set_option_int(FFPlayer *ffp, int opt_category, const char *name, int64_t value)
{
    if (!ffp || !name)
        return;

    AVDictionary **dict = NULL;

    switch (opt_category) {
    case FFP_OPT_CATEGORY_FORMAT:
        dict = &ffp->format_opts;
        break;
    case FFP_OPT_CATEGORY_CODEC:
        dict = &ffp->codec_opts;
        break;
    case FFP_OPT_CATEGORY_SWS:
        dict = &ffp->sws_dict;
        break;
    case FFP_OPT_CATEGORY_SWR:
        dict = &ffp->swr_opts;
        break;
    case FFP_OPT_CATEGORY_PLAYER:
        /* 播放器选项直接设置到 FFPlayer 结构体 */
        /* 这里可以添加具体的选项处理 */
        return;
    default:
        return;
    }

    if (dict) {
        av_dict_set_int(dict, name, value, 0);
    }
}

/*
 * =============================================================================
 * 属性访问
 * =============================================================================
 */

float ffp_get_property_float(FFPlayer *ffp, int id, float default_value)
{
    if (!ffp)
        return default_value;

    switch (id) {
    case FFP_PROP_FLOAT_PLAYBACK_RATE:
        return ffp->playback_rate;
    case FFP_PROP_FLOAT_PLAYBACK_VOLUME:
        return ffp_get_volume(ffp);
    default:
        return default_value;
    }
}

void ffp_set_property_float(FFPlayer *ffp, int id, float value)
{
    if (!ffp)
        return;

    switch (id) {
    case FFP_PROP_FLOAT_PLAYBACK_RATE:
        ffp_set_playback_rate(ffp, value);
        break;
    case FFP_PROP_FLOAT_PLAYBACK_VOLUME:
        ffp_set_volume(ffp, value);
        break;
    default:
        break;
    }
}

int64_t ffp_get_property_int64(FFPlayer *ffp, int id, int64_t default_value)
{
    if (!ffp)
        return default_value;

    switch (id) {
    case FFP_PROP_INT64_CURRENT_POSITION:
        return ffp_get_current_position(ffp);
    case FFP_PROP_INT64_DURATION:
        return ffp_get_duration(ffp);
    case FFP_PROP_INT64_VIDEO_CACHED_BYTES:
        if (ffp->is)
            return ffp->is->videoq.size;
        return 0;
    case FFP_PROP_INT64_AUDIO_CACHED_BYTES:
        if (ffp->is)
            return ffp->is->audioq.size;
        return 0;
    case FFP_PROP_INT64_VIDEO_CACHED_PACKETS:
        if (ffp->is)
            return ffp->is->videoq.nb_packets;
        return 0;
    case FFP_PROP_INT64_AUDIO_CACHED_PACKETS:
        if (ffp->is)
            return ffp->is->audioq.nb_packets;
        return 0;
    default:
        return default_value;
    }
}

void ffp_set_property_int64(FFPlayer *ffp, int id, int64_t value)
{
    if (!ffp)
        return;

    switch (id) {
    /* 大多数 INT64 属性是只读的，这里只是预留接口 */
    default:
        break;
    }
    (void)value; /* 避免未使用警告 */
}

/*
 * =============================================================================
 * 可选/后续实现的函数（当前空实现）
 * =============================================================================
 */

void ffp_set_playback_rate(FFPlayer *ffp, float rate)
{
    if (!ffp)
        return;

    /* 保存播放速率值，但 ffplay 目前不支持变速播放 */
    /* 后续可以通过 atempo 音频滤镜和调整视频帧率来实现 */
    ffp->playback_rate = rate;

    av_log(NULL, AV_LOG_WARNING,
           "ffp_set_playback_rate: playback rate %.2f is set but not implemented yet\n",
           rate);
}

int ffp_get_video_rotate_degrees(FFPlayer *ffp)
{
    /* 当前空实现，后续可以通过读取 displaymatrix side data 来实现 */
    (void)ffp;
    return 0;
}
