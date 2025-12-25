/*
 * Copyright (c) 2003 Fabrice Bellard
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

/**
 * @file
 * ffplay stream management and audio/video output implementations
 */

#include "ffplay.h"
#include "ff_vout.h"
#include "ff_ffmsg.h"
#include "ff_audio_mixer.h"

/* 消息通知函数声明（在 mediaplayer.c 中实现）*/
extern void ffp_notify_msg1(FFPlayer *ffp, int what);
extern void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1);
extern void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2);

/* 硬件加速相关函数声明（在 ff_ffplayer.c 中实现）*/
extern enum AVHWDeviceType ffp_get_av_hwdevice_type(FFPHWAccelType type);
extern const char *ffp_get_hwaccel_name(FFPHWAccelType type);

/*
 * =============================================================================
 * 硬件解码支持
 * =============================================================================
 */

/* 获取硬件像素格式回调 */
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    FFPlayer *ffp = (FFPlayer *)ctx->opaque;
    const enum AVPixelFormat *p;

    /* 首先尝试找到硬件格式 */
    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == ffp->hw_pix_fmt) {
            av_log(NULL, AV_LOG_INFO, "[HWAccel] Using hardware pixel format: %s\n",
                   av_get_pix_fmt_name(*p));
            return *p;
        }
    }

    /* 硬件格式不可用，回退到第一个软件格式 */
    av_log(NULL, AV_LOG_WARNING, "[HWAccel] Hardware format %s not in list, falling back to software\n",
           av_get_pix_fmt_name(ffp->hw_pix_fmt));
    ffp->hwaccel_failed = 1;
    
    /* 返回列表中的第一个有效格式（通常是软件格式）*/
    if (pix_fmts[0] != AV_PIX_FMT_NONE) {
        av_log(NULL, AV_LOG_INFO, "[HWAccel] Fallback to: %s\n",
               av_get_pix_fmt_name(pix_fmts[0]));
        return pix_fmts[0];
    }
    
    return AV_PIX_FMT_NONE;
}

/* 初始化硬件解码器 */
static int hw_decoder_init(FFPlayer *ffp, AVCodecContext *ctx, enum AVHWDeviceType type)
{
    int ret;
    AVBufferRef *hw_device_ctx = NULL;

    ret = av_hwdevice_ctx_create(&hw_device_ctx, type, ffp->hwaccel_device, NULL, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "[HWAccel] Failed to create %s device: %s\n",
               av_hwdevice_get_type_name(type), av_err2str(ret));
        return ret;
    }

    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    if (!ctx->hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
        return AVERROR(ENOMEM);
    }

    /* 保存到 FFPlayer 以便后续清理 */
    if (ffp->hw_device_ctx) {
        av_buffer_unref((AVBufferRef **)&ffp->hw_device_ctx);
    }
    ffp->hw_device_ctx = hw_device_ctx;

    av_log(NULL, AV_LOG_INFO, "[HWAccel] Hardware device created: %s\n",
           av_hwdevice_get_type_name(type));
    return 0;
}

/* 选择最佳的硬件加速类型（用于 AUTO 模式）*/
static FFPHWAccelType select_best_hwaccel(const AVCodec *codec)
{
    /* 按优先级尝试不同的硬件加速 */
    static const FFPHWAccelType preferred_types[] = {
#ifdef _WIN32
        FFP_HWACCEL_D3D11VA,
        FFP_HWACCEL_DXVA2,
#endif
        FFP_HWACCEL_CUDA,
#ifdef __linux__
        FFP_HWACCEL_VAAPI,
        FFP_HWACCEL_VDPAU,
#endif
#ifdef __APPLE__
        FFP_HWACCEL_VIDEOTOOLBOX,
#endif
        FFP_HWACCEL_QSV,
    };

    for (int i = 0; i < sizeof(preferred_types) / sizeof(preferred_types[0]); i++) {
        FFPHWAccelType type = preferred_types[i];
        enum AVHWDeviceType av_type = ffp_get_av_hwdevice_type(type);
        
        if (av_type == AV_HWDEVICE_TYPE_NONE) {
            continue;
        }

        /* 检查编解码器是否支持此硬件加速 */
        for (int j = 0;; j++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
            if (!config) {
                break;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == av_type) {
                /* 尝试创建设备上下文验证可用性 */
                AVBufferRef *test_ctx = NULL;
                int ret = av_hwdevice_ctx_create(&test_ctx, av_type, NULL, NULL, 0);
                if (ret >= 0) {
                    av_buffer_unref(&test_ctx);
                    av_log(NULL, AV_LOG_INFO, "[HWAccel] Auto-selected: %s\n",
                           ffp_get_hwaccel_name(type));
                    return type;
                }
            }
        }
    }

    return FFP_HWACCEL_NONE;
}

/* 为视频解码器配置硬件加速 */
static int configure_hwaccel(FFPlayer *ffp, AVCodecContext *avctx, const AVCodec *codec)
{
    FFPHWAccelType hwaccel_type = ffp->hwaccel_type;
    enum AVHWDeviceType av_hw_type;
    int ret;

    /* 软解码直接返回 */
    if (hwaccel_type == FFP_HWACCEL_NONE) {
        av_log(NULL, AV_LOG_INFO, "[HWAccel] Using software decoding\n");
        return 0;
    }

    /* 自动选择最佳硬件加速 */
    if (hwaccel_type == FFP_HWACCEL_AUTO) {
        hwaccel_type = select_best_hwaccel(codec);
        if (hwaccel_type == FFP_HWACCEL_NONE) {
            av_log(NULL, AV_LOG_INFO, "[HWAccel] No suitable hardware accelerator found\n");
            return 0;
        }
    }

    av_hw_type = ffp_get_av_hwdevice_type(hwaccel_type);
    if (av_hw_type == AV_HWDEVICE_TYPE_NONE) {
        av_log(NULL, AV_LOG_WARNING, "[HWAccel] Invalid hwaccel type: %d\n", hwaccel_type);
        return 0;
    }

    /* 查找编解码器的硬件配置 */
    const AVCodecHWConfig *config = NULL;
    for (int i = 0;; i++) {
        config = avcodec_get_hw_config(codec, i);
        if (!config) {
            av_log(NULL, AV_LOG_WARNING, 
                   "[HWAccel] Codec %s does not support %s\n",
                   codec->name, av_hwdevice_get_type_name(av_hw_type));
            return 0;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == av_hw_type) {
            ffp->hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    /* 初始化硬件设备 */
    ret = hw_decoder_init(ffp, avctx, av_hw_type);
    if (ret < 0) {
        av_log(NULL, AV_LOG_WARNING, 
               "[HWAccel] Failed to init %s, falling back to software\n",
               ffp_get_hwaccel_name(hwaccel_type));
        ffp->hwaccel_failed = 1;
        ffp->hw_pix_fmt = AV_PIX_FMT_NONE;  /* 重置，避免错误的帧格式判断 */
        return 0;
    }

    /* 设置像素格式回调 */
    avctx->opaque = ffp;
    avctx->get_format = get_hw_format;

    av_log(NULL, AV_LOG_INFO, 
           "[HWAccel] Configured %s for codec %s, pixel format: %s\n",
           ffp_get_hwaccel_name(hwaccel_type), codec->name,
           av_get_pix_fmt_name(ffp->hw_pix_fmt));

    return 1;  /* 硬件加速配置成功 */
}

/* 将硬件帧转换为软件帧 */
static int hw_frame_to_sw(AVFrame *hw_frame, AVFrame *sw_frame)
{
    int ret;

    /* 分配软件帧缓冲 */
    ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "[HWAccel] Error transferring data from GPU: %s\n",
               av_err2str(ret));
        return ret;
    }

    /* 复制帧属性 */
    ret = av_frame_copy_props(sw_frame, hw_frame);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "[HWAccel] Error copying frame props: %s\n",
               av_err2str(ret));
        return ret;
    }

    return 0;
}

/* 将 AVPixelFormat 映射到 FFVoutPixelFormat */
static FFVoutPixelFormat av_to_vout_format(enum AVPixelFormat format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
        return VOUT_FMT_YUV420P;
    case AV_PIX_FMT_RGBA:
        return VOUT_FMT_RGBA;
    case AV_PIX_FMT_BGRA:
        return VOUT_FMT_BGRA;
    default:
        return VOUT_FMT_UNKNOWN;
    }
}

#if CONFIG_AVFILTER
int opt_add_vfilter(FFPlayer *ffp, void *optctx, const char *opt, const char *arg)
{
    GROW_ARRAY(ffp->vfilters_list, ffp->nb_vfilters);
    ffp->vfilters_list[ffp->nb_vfilters - 1] = arg;
    return 0;
}
#endif

static inline
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2)
{
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

void fill_rectangle(FFPlayer *ffp, int x, int y, int w, int h)
{
    if (w && h)
        vout_fill_rect(ffp->vout, x, y, w, h);
}

int realloc_texture(FFPlayer *ffp, FFVoutTexture **texture, int new_width, int new_height, FFVoutPixelFormat format, int init_texture)
{
    (void)init_texture; /* OpenGL 纹理默认初始化为 0 */
    return vout_texture_realloc(ffp->vout, texture, new_width, new_height, format);
}

void calculate_display_rect(SDL_Rect *rect,
                                   int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                   int pic_width, int pic_height, AVRational pic_sar)
{
    AVRational aspect_ratio = pic_sar;
    int64_t width, height, x, y;

    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0)
        aspect_ratio = av_make_q(1, 1);

    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(pic_width, pic_height));

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > scr_width) {
        width = scr_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    rect->x = scr_xleft + x;
    rect->y = scr_ytop  + y;
    rect->w = FFMAX((int)width,  1);
    rect->h = FFMAX((int)height, 1);
}

/* 判断是否为 YUV420P 格式 */
static int is_yuv420p_format(enum AVPixelFormat format)
{
    return format == AV_PIX_FMT_YUV420P ||
           format == AV_PIX_FMT_YUVJ420P;
}

int upload_texture(FFPlayer *ffp, FFVoutTexture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx) {
    int ret = 0;
    
    if (is_yuv420p_format(frame->format)) {
        /* 直接使用 YUV420P */
        if (realloc_texture(ffp, tex, frame->width, frame->height, VOUT_FMT_YUV420P, 0) < 0)
            return -1;
        
        if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0) {
            ret = vout_texture_upload_yuv420p(*tex,
                frame->data[0], frame->linesize[0],
                frame->data[1], frame->linesize[1],
                frame->data[2], frame->linesize[2],
                frame->width, frame->height);
        } else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0) {
            /* 负 linesize 表示图像翻转，需要调整数据指针 */
            ret = vout_texture_upload_yuv420p(*tex,
                frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0],
                frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
                frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2],
                frame->width, frame->height);
        } else {
            av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
            return -1;
        }
    } else {
        /* 其他格式转换为 BGRA */
        if (realloc_texture(ffp, tex, frame->width, frame->height, VOUT_FMT_BGRA, 0) < 0)
            return -1;
        
        *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
            frame->width, frame->height, frame->format,
            frame->width, frame->height, AV_PIX_FMT_BGRA,
            ffp->sws_flags, NULL, NULL, NULL);
        
        if (*img_convert_ctx != NULL) {
            uint8_t *pixels;
            int pitch;
            if (vout_texture_lock(*tex, (void **)&pixels, &pitch) == 0) {
                uint8_t *dst[4] = { pixels, NULL, NULL, NULL };
                int dst_linesize[4] = { pitch, 0, 0, 0 };
                sws_scale(*img_convert_ctx, (const uint8_t * const *)frame->data, frame->linesize,
                          0, frame->height, dst, dst_linesize);
                vout_texture_unlock(*tex);
            }
        } else {
            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
            ret = -1;
        }
    }
    return ret;
}

void set_sdl_yuv_conversion_mode(AVFrame *frame)
{
    /* OpenGL 着色器已经处理了 YUV 到 RGB 的转换，无需额外设置 */
    (void)frame;
}

void video_image_display(FFPlayer *ffp, VideoState *is)
{
    static int first_frame_rendered = 0;  /* 首帧渲染标志 */
    Frame *vp;
    Frame *sp = NULL;
    SDL_Rect rect;
    FFVoutRect dst_rect;

    vp = frame_queue_peek_last(&is->pictq);
    
    /* 处理字幕 */
    if (is->subtitle_st) {
        if (frame_queue_nb_remaining(&is->subpq) > 0) {
            sp = frame_queue_peek(&is->subpq);

            if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
                if (!sp->uploaded) {
                    int i;
                    if (!sp->width || !sp->height) {
                        sp->width = vp->width;
                        sp->height = vp->height;
                    }
                    if (realloc_texture(ffp, &is->sub_texture, sp->width, sp->height, VOUT_FMT_BGRA, 1) < 0)
                        return;

                    for (i = 0; i < sp->sub.num_rects; i++) {
                        AVSubtitleRect *sub_rect = sp->sub.rects[i];
                        uint8_t *pixels;
                        int pitch;

                        sub_rect->x = av_clip(sub_rect->x, 0, sp->width );
                        sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
                        sub_rect->w = av_clip(sub_rect->w, 0, sp->width  - sub_rect->x);
                        sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

                        is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
                            sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
                            sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
                            0, NULL, NULL, NULL);
                        if (!is->sub_convert_ctx) {
                            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                            return;
                        }
                        if (vout_texture_lock(is->sub_texture, (void **)&pixels, &pitch) == 0) {
                            uint8_t *dst[4] = { pixels, NULL, NULL, NULL };
                            int dst_linesize[4] = { pitch, 0, 0, 0 };
                            sws_scale(is->sub_convert_ctx, (const uint8_t * const *)sub_rect->data, sub_rect->linesize,
                                      0, sub_rect->h, dst, dst_linesize);
                            vout_texture_unlock(is->sub_texture);
                        }
                    }
                    sp->uploaded = 1;
                }
            } else
                sp = NULL;
        }
    }

    calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);

    if (!vp->uploaded) {
        if (upload_texture(ffp, &is->vid_texture, vp->frame, &is->img_convert_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR, "[IMAGE] upload_texture failed\n");
            return;
        }
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
        
        /* 首帧渲染通知 */
        if (!first_frame_rendered) {
            first_frame_rendered = 1;
            ffp_notify_msg1(ffp, FFP_MSG_VIDEO_RENDERING_START);
            av_log(NULL, AV_LOG_INFO, "[IMAGE] First frame rendered\n");
        }
    }

    /* 转换 SDL_Rect 到 FFVoutRect */
    dst_rect.x = rect.x;
    dst_rect.y = rect.y;
    dst_rect.w = rect.w;
    dst_rect.h = rect.h;

    /* 绘制视频纹理 */
    vout_draw_texture(ffp->vout, is->vid_texture, NULL, &dst_rect, vp->flip_v);
    
    /* 绘制字幕 */
    if (sp) {
        vout_draw_texture_blend(ffp->vout, is->sub_texture, NULL, &dst_rect);
    }
}

int compute_mod(int a, int b)
{
    return a < 0 ? a%b + b : a%b;
}

void video_audio_display(FFPlayer *ffp, VideoState *s)
{
    int i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
    int ch, channels, h, h2;
    int64_t time_diff;
    int rdft_bits, nb_freq;

    for (rdft_bits = 1; (1 << rdft_bits) < 2 * s->height; rdft_bits++)
        ;
    nb_freq = 1 << (rdft_bits - 1);

    /* compute display index : center on currently output samples */
    channels = s->audio_tgt.ch_layout.nb_channels;
    nb_display_channels = channels;
    if (!s->paused) {
        int data_used= s->show_mode == SHOW_MODE_WAVES ? s->width : (2*nb_freq);
        n = 2 * channels;
        delay = s->audio_write_buf_size;
        delay /= n;

        /* to be more precise, we take into account the time spent since
           the last buffer computation */
        if (ffp->audio_callback_time) {
            time_diff = av_gettime_relative() - ffp->audio_callback_time;
            delay -= (time_diff * s->audio_tgt.freq) / 1000000;
        }

        delay += 2 * data_used;
        if (delay < data_used)
            delay = data_used;

        i_start= x = compute_mod(s->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
        if (s->show_mode == SHOW_MODE_WAVES) {
            h = INT_MIN;
            for (i = 0; i < 1000; i += channels) {
                int idx = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
                int a = s->sample_array[idx];
                int b = s->sample_array[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
                int c = s->sample_array[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
                int d = s->sample_array[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
                int score = a - d;
                if (h < score && (b ^ c) < 0) {
                    h = score;
                    i_start = idx;
                }
            }
        }

        s->last_i_start = i_start;
    } else {
        i_start = s->last_i_start;
    }

    if (s->show_mode == SHOW_MODE_WAVES) {
        vout_set_draw_color(ffp->vout, 255, 255, 255, 255);

        /* total height for one channel */
        h = s->height / nb_display_channels;
        /* graph height / 2 */
        h2 = (h * 9) / 20;
        for (ch = 0; ch < nb_display_channels; ch++) {
            i = i_start + ch;
            y1 = s->ytop + ch * h + (h / 2); /* position of center line */
            for (x = 0; x < s->width; x++) {
                y = (s->sample_array[i] * h2) >> 15;
                if (y < 0) {
                    y = -y;
                    ys = y1 - y;
                } else {
                    ys = y1;
                }
                fill_rectangle(ffp, s->xleft + x, ys, 1, y);
                i += channels;
                if (i >= SAMPLE_ARRAY_SIZE)
                    i -= SAMPLE_ARRAY_SIZE;
            }
        }

        vout_set_draw_color(ffp->vout, 0, 0, 255, 255);

        for (ch = 1; ch < nb_display_channels; ch++) {
            y = s->ytop + ch * h;
            fill_rectangle(ffp, s->xleft, y, s->width, 1);
        }
    } else {
        /* RDFT 频谱显示模式 - 简化处理，暂不支持 */
        if (realloc_texture(ffp, &s->vis_texture, s->width, s->height, VOUT_FMT_BGRA, 1) < 0)
            return;

        if (s->xpos >= s->width)
            s->xpos = 0;
        nb_display_channels= FFMIN(nb_display_channels, 2);
        if (rdft_bits != s->rdft_bits) {
            av_rdft_end(s->rdft);
            av_free(s->rdft_data);
            s->rdft = av_rdft_init(rdft_bits, DFT_R2C);
            s->rdft_bits = rdft_bits;
            s->rdft_data = av_malloc_array(nb_freq, 4 *sizeof(*s->rdft_data));
        }
        if (!s->rdft || !s->rdft_data){
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate buffers for RDFT, switching to waves display\n");
            s->show_mode = SHOW_MODE_WAVES;
        } else {
            FFTSample *data[2];
            uint32_t *pixels;
            int pitch;
            for (ch = 0; ch < nb_display_channels; ch++) {
                data[ch] = s->rdft_data + 2 * nb_freq * ch;
                i = i_start + ch;
                for (x = 0; x < 2 * nb_freq; x++) {
                    double w = (x-nb_freq) * (1.0 / nb_freq);
                    data[ch][x] = s->sample_array[i] * (1.0 - w * w);
                    i += channels;
                    if (i >= SAMPLE_ARRAY_SIZE)
                        i -= SAMPLE_ARRAY_SIZE;
                }
                av_rdft_calc(s->rdft, data[ch]);
            }
            /* 使用 vout 锁定纹理 */
            if (vout_texture_lock(s->vis_texture, (void **)&pixels, &pitch) == 0) {
                pitch >>= 2;
                pixels += pitch * s->height;
                for (y = 0; y < s->height; y++) {
                    double w = 1 / sqrt(nb_freq);
                    int a = sqrt(w * sqrt(data[0][2 * y + 0] * data[0][2 * y + 0] + data[0][2 * y + 1] * data[0][2 * y + 1]));
                    int b = (nb_display_channels == 2 ) ? sqrt(w * hypot(data[1][2 * y + 0], data[1][2 * y + 1]))
                                                        : a;
                    a = FFMIN(a, 255);
                    b = FFMIN(b, 255);
                    pixels -= pitch;
                    *pixels = (a << 16) + (b << 8) + ((a+b) >> 1);
                }
                vout_texture_unlock(s->vis_texture);
            }
            /* 绘制可视化纹理 */
            FFVoutRect full_rect = {0, 0, s->width, s->height};
            vout_draw_texture(ffp->vout, s->vis_texture, NULL, &full_rect, 0);
        }
        if (!s->paused)
            s->xpos++;
    }
}

void stream_component_close(FFPlayer *ffp, VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        av_log(NULL, AV_LOG_INFO, "[AUDIO-CLOSE] is=%p ffp=%p - Starting audio close (mixer mode)\n", 
               is, ffp);
        
        /* 禁用音频回调 */
        is->audio_callback_enabled = 0;
        
        decoder_abort(&is->auddec, &is->sampq);
        
        /* 等待音频馈送线程退出 */
        if (is->audio_feeder_tid) {
            av_log(NULL, AV_LOG_INFO, "[AUDIO-CLOSE] is=%p - Waiting for audio feeder thread\n", is);
            SDL_WaitThread(is->audio_feeder_tid, NULL);
            is->audio_feeder_tid = NULL;
            av_log(NULL, AV_LOG_INFO, "[AUDIO-CLOSE] is=%p - Audio feeder thread stopped\n", is);
        }
        
        /* 从混音器中移除流 */
        if (is->mixer_stream) {
            av_log(NULL, AV_LOG_INFO, "[AUDIO-CLOSE] is=%p - Removing stream from mixer\n", is);
            audio_stream_stop(is->mixer_stream);
            audio_mixer_remove_stream(audio_mixer_get_global(), is->mixer_stream);
            is->mixer_stream = NULL;
        }
        
        decoder_destroy(&is->auddec);
        swr_free(&is->swr_ctx);
        av_freep(&is->audio_buf1);
        is->audio_buf1_size = 0;
        is->audio_buf = NULL;

        if (is->rdft) {
            av_rdft_end(is->rdft);
            av_freep(&is->rdft_data);
            is->rdft = NULL;
            is->rdft_bits = 0;
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        decoder_abort(&is->viddec, &is->pictq);
        decoder_destroy(&is->viddec);
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        decoder_abort(&is->subdec, &is->subpq);
        decoder_destroy(&is->subdec);
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_st = NULL;
        is->subtitle_stream = -1;
        break;
    default:
        break;
    }
}

void stream_close(FFPlayer *ffp, VideoState *is)
{
    av_log(NULL, AV_LOG_INFO, "[STREAM-CLOSE] ffp=%p is=%p - Starting stream close\n", ffp, is);
    
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    av_log(NULL, AV_LOG_INFO, "[STREAM-CLOSE] is=%p - Waiting for read thread\n", is);
    SDL_WaitThread(is->read_tid, NULL);
    av_log(NULL, AV_LOG_INFO, "[STREAM-CLOSE] is=%p - Read thread finished\n", is);

    /* close each stream */
    if (is->audio_stream >= 0) {
        av_log(NULL, AV_LOG_INFO, "[STREAM-CLOSE] is=%p - Closing audio stream %d\n", is, is->audio_stream);
        stream_component_close(ffp, is, is->audio_stream);
    }
    if (is->video_stream >= 0)
        stream_component_close(ffp, is, is->video_stream);
    if (is->subtitle_stream >= 0)
        stream_component_close(ffp, is, is->subtitle_stream);

    /* 清理硬件设备上下文 */
    if (ffp->hw_device_ctx) {
        av_buffer_unref((AVBufferRef **)&ffp->hw_device_ctx);
        ffp->hw_device_ctx = NULL;
    }

    avformat_close_input(&is->ic);

    packet_queue_destroy(&is->videoq);
    packet_queue_destroy(&is->audioq);
    packet_queue_destroy(&is->subtitleq);

    /* free all pictures */
    frame_queue_destory(&is->pictq);
    frame_queue_destory(&is->sampq);
    frame_queue_destory(&is->subpq);
    SDL_DestroyCond(is->continue_read_thread);
    sws_freeContext(is->img_convert_ctx);
    sws_freeContext(is->sub_convert_ctx);
    av_free(is->filename);
    if (is->vis_texture)
        vout_texture_destroy(is->vis_texture);
    if (is->vid_texture)
        vout_texture_destroy(is->vid_texture);
    if (is->sub_texture)
        vout_texture_destroy(is->sub_texture);
    
    /* 短暂延迟确保所有清理操作完成 */
    av_log(NULL, AV_LOG_INFO, "[STREAM-CLOSE] is=%p - Waiting before free\n", is);
    SDL_Delay(50);
    
    /* 清零内存，使悬空指针访问更容易被检测 */
    av_log(NULL, AV_LOG_INFO, "[STREAM-CLOSE] is=%p - Clearing and freeing VideoState\n", is);
    memset(is, 0, sizeof(VideoState));
    av_free(is);
    av_log(NULL, AV_LOG_INFO, "[STREAM-CLOSE] VideoState freed\n");
}

void set_default_window_size(FFPlayer *ffp, int width, int height, AVRational sar)
{
    SDL_Rect rect;
    int max_width  = ffp->screen_width  ? ffp->screen_width  : INT_MAX;
    int max_height = ffp->screen_height ? ffp->screen_height : INT_MAX;
    if (max_width == INT_MAX && max_height == INT_MAX)
        max_height = height;
    calculate_display_rect(&rect, 0, 0, max_width, max_height, width, height, sar);
    ffp->default_width  = rect.w;
    ffp->default_height = rect.h;
}

int video_open(FFPlayer *ffp, VideoState *is)
{
    int w,h;

    w = ffp->screen_width ? ffp->screen_width : ffp->default_width;
    h = ffp->screen_height ? ffp->screen_height : ffp->default_height;

    /* 回调模式：不需要窗口操作，只设置尺寸 */
    if (ffp->render_mode == FFP_RENDER_MODE_CALLBACK) {
        /* 从视频流获取实际尺寸 */
        if (is->video_st && is->video_st->codecpar) {
            w = is->video_st->codecpar->width;
            h = is->video_st->codecpar->height;
        }
        is->width = w;
        is->height = h;
        av_log(NULL, AV_LOG_INFO, "[VIDEO_OPEN] Callback mode: %dx%d\n", w, h);
        ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, w, h);
        return 0;
    }

    /* SDL 模式：窗口操作 */
    if (ffp->window) {
    /* 对于从原生窗口创建的 SDL 窗口，不要修改窗口属性 */
    if (!ffp->native_window) {
    if (!ffp->window_title)
        ffp->window_title = ffp->input_filename;
    SDL_SetWindowTitle(ffp->window, ffp->window_title);

    SDL_SetWindowSize(ffp->window, w, h);
    SDL_SetWindowPosition(ffp->window, ffp->screen_left, ffp->screen_top);
    if (ffp->is_full_screen)
        SDL_SetWindowFullscreen(ffp->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_ShowWindow(ffp->window);
    } else {
        /* 对于嵌入式窗口，使用实际窗口大小 */
        SDL_GetWindowSize(ffp->window, &w, &h);
    }

    /* 更新 OpenGL 视口大小 */
        if (ffp->vout) {
    vout_set_size(ffp->vout, w, h);
        }
    }

    is->width  = w;
    is->height = h;
    
    /* 发送视频尺寸变化消息 */
    ffp_notify_msg3(ffp, FFP_MSG_VIDEO_SIZE_CHANGED, w, h);
    
    /* 确保渲染线程已启动（视频流打开后启动） */
    if (ffp->auto_render_enabled && !ffp->render_tid) {
        ffp_start_render_thread(ffp);
    }

    return 0;
}

/* display the current picture, if any */
void video_display(FFPlayer *ffp, VideoState *is)
{
    if (!is->width) {
        video_open(ffp, is);
    }

    /* 回调模式：通过回调传递帧数据给外部渲染 */
    if (ffp->render_mode == FFP_RENDER_MODE_CALLBACK) {
        if (is->video_st && ffp->video_frame_cb) {
            Frame *vp = frame_queue_peek_last(&is->pictq);
            if (vp && vp->frame) {
                FFPVideoFrame frame;
                frame.data[0] = vp->frame->data[0];
                frame.data[1] = vp->frame->data[1];
                frame.data[2] = vp->frame->data[2];
                frame.data[3] = vp->frame->data[3];
                frame.linesize[0] = vp->frame->linesize[0];
                frame.linesize[1] = vp->frame->linesize[1];
                frame.linesize[2] = vp->frame->linesize[2];
                frame.linesize[3] = vp->frame->linesize[3];
                frame.width = vp->frame->width;
                frame.height = vp->frame->height;
                frame.format = vp->frame->format;
                frame.pts = vp->pts;
                frame.pos = vp->pos;
                
                ffp->video_frame_cb(ffp->video_frame_cb_opaque, &frame);
            }
        }
        return;  /* 回调模式直接返回，不执行 SDL 渲染 */
    }

    /* SDL 模式：原有渲染逻辑 */
    vout_render_begin(ffp->vout);
    vout_clear(ffp->vout, 0, 0, 0);
    if (is->audio_st && is->show_mode != SHOW_MODE_VIDEO)
        video_audio_display(ffp, is);
    else if (is->video_st) {
        video_image_display(ffp, is);
    }
    vout_render_present(ffp->vout);
}

/* seek in the stream */
void stream_seek(VideoState *is, int64_t pos, int64_t rel, int by_bytes)
{
    if (!is->seek_req) {
        is->seek_pos = pos;
        is->seek_rel = rel;
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (by_bytes)
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        is->seek_req = 1;
        SDL_CondSignal(is->continue_read_thread);
    }
}

/* pause or resume the video */
void stream_toggle_pause(VideoState *is)
{
    if (is->paused) {
        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
    }
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

void toggle_pause(VideoState *is)
{
    stream_toggle_pause(is);
    is->step = 0;
}

void toggle_mute(VideoState *is)
{
    is->muted = !is->muted;
}

void update_volume(VideoState *is, int sign, double step)
{
    double volume_level = is->audio_volume ? (20 * log(is->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    is->audio_volume = av_clip(is->audio_volume == new_volume ? (is->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

void step_to_next_frame(VideoState *is)
{
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        stream_toggle_pause(is);
    is->step = 1;
}

/* called to display each frame */
void video_refresh(FFPlayer *ffp, VideoState *is, double *remaining_time)
{
    double time;

    Frame *sp, *sp2;

    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    if (!ffp->display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
        time = av_gettime_relative() / 1000000.0;
        if (is->force_refresh || is->last_vis_time + ffp->rdftspeed < time) {
            video_display(ffp, is);
            is->last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + ffp->rdftspeed - time);
    }

    if (is->video_st) {
retry:
        if (frame_queue_nb_remaining(&is->pictq) == 0) {
            // nothing to do, no picture to display in the queue
        } else {
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            lastvp = frame_queue_peek_last(&is->pictq);
            vp = frame_queue_peek(&is->pictq);

            if (vp->serial != is->videoq.serial) {
                frame_queue_next(&is->pictq);
                goto retry;
            }

            if (lastvp->serial != vp->serial)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            if (is->paused)
                goto display;

            last_duration = vp_duration(is, lastvp, vp);
            
            /* 
             * 使用 compute_target_delay 计算延迟，倍速逻辑已集成在其中：
             * - 正常播放：使用标准的 A-V 同步
             * - 倍速播放：delay 会除以 playback_rate，同步阈值也会相应调整
             */
            delay = compute_target_delay(last_duration, is, ffp->playback_rate);

            time= av_gettime_relative()/1000000.0;
            if (time < is->frame_timer + delay) {
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                goto display;
            }

            is->frame_timer += delay;
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            SDL_LockMutex(is->pictq.mutex);
            if (!isnan(vp->pts))
                update_video_pts(is, vp->pts, vp->pos, vp->serial);
            SDL_UnlockMutex(is->pictq.mutex);
            
            /* 日志：定期打印音视频同步状态 */
            {
                static int video_log_counter = 0;
                static double last_vp_pts = 0;
                if (++video_log_counter >= 150) {  // 每150帧打印一次（约5秒@30fps）
                    video_log_counter = 0;
                    double audclk = get_clock(&is->audclk);
                    double vidclk = get_clock(&is->vidclk);
                    double av_diff = vidclk - audclk;
                    av_log(NULL, AV_LOG_INFO, 
                           "[AV-SYNC] vp_pts=%.3f audclk=%.3f vidclk=%.3f diff=%.4f "
                           "delay=%.4f rate=%.2f vp_delta=%.3f\n",
                           vp->pts, audclk, vidclk, av_diff,
                           delay, ffp->playback_rate, vp->pts - last_vp_pts);
                    last_vp_pts = vp->pts;
                }
            }

            if (frame_queue_nb_remaining(&is->pictq) > 1) {
                Frame *nextvp = frame_queue_peek_next(&is->pictq);
                duration = vp_duration(is, vp, nextvp);
                /* 倍速播放时调整丢帧判断的 duration */
                if (ffp->playback_rate > 0.001f && fabsf(ffp->playback_rate - 1.0f) > 0.001f) {
                    duration = duration / ffp->playback_rate;
                }
                if(!is->step && (ffp->framedrop>0 || (ffp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration){
                    is->frame_drops_late++;
                    frame_queue_next(&is->pictq);
                    goto retry;
                }
            }

            if (is->subtitle_st) {
                while (frame_queue_nb_remaining(&is->subpq) > 0) {
                    sp = frame_queue_peek(&is->subpq);

                    if (frame_queue_nb_remaining(&is->subpq) > 1)
                        sp2 = frame_queue_peek_next(&is->subpq);
                    else
                        sp2 = NULL;

                    if (sp->serial != is->subtitleq.serial
                            || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                            || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                    {
                        if (sp->uploaded) {
                            int i;
                            for (i = 0; i < sp->sub.num_rects; i++) {
                                AVSubtitleRect *sub_rect = sp->sub.rects[i];
                                uint8_t *pixels;
                                int pitch, j;

                                if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
                                    for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                        memset(pixels, 0, sub_rect->w << 2);
                                    SDL_UnlockTexture(is->sub_texture);
                                }
                            }
                        }
                        frame_queue_next(&is->subpq);
                    } else {
                        break;
                    }
                }
            }

            frame_queue_next(&is->pictq);
            is->force_refresh = 1;

            if (is->step && !is->paused)
                stream_toggle_pause(is);
        }
display:
        if (!ffp->display_disable && is->force_refresh && is->show_mode == SHOW_MODE_VIDEO && is->pictq.rindex_shown) {
            video_display(ffp, is);
        }
    }
    is->force_refresh = 0;
    if (ffp->show_status) {
        AVBPrint buf;
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        cur_time = av_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (is->audio_st)
                aqsize = is->audioq.size;
            if (is->video_st)
                vqsize = is->videoq.size;
            if (is->subtitle_st)
                sqsize = is->subtitleq.size;
            av_diff = 0;
            if (is->audio_st && is->video_st)
                av_diff = get_clock(&is->audclk) - get_clock(&is->vidclk);
            else if (is->video_st)
                av_diff = get_master_clock(is) - get_clock(&is->vidclk);
            else if (is->audio_st)
                av_diff = get_master_clock(is) - get_clock(&is->audclk);

            av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
            av_bprintf(&buf,
                      "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
                      get_master_clock(is),
                      (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
                      av_diff,
                      is->frame_drops_early + is->frame_drops_late,
                      aqsize / 1024,
                      vqsize / 1024,
                      sqsize,
                      is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
                      is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);

            if (ffp->show_status == 1 && AV_LOG_INFO > av_log_get_level())
                fprintf(stderr, "%s", buf.str);
            else
                av_log(NULL, AV_LOG_INFO, "%s", buf.str);

            fflush(stderr);
            av_bprint_finalize(&buf, NULL);

            last_time = cur_time;
        }
    }
}

int queue_picture(FFPlayer *ffp, VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;
    static int last_serial = -1;
    static double last_pts = 0;

#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif

    if (!(vp = frame_queue_peek_writable(&is->pictq)))
        return -1;

    vp->sar = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;
    
    /* 检测 seek 后的第一帧（serial 变化）或 PTS 跳变 */
    if (serial != last_serial) {
        av_log(NULL, AV_LOG_INFO, 
               "[VIDEO-QUEUE] First frame after seek: pts=%.3f serial=%d->%d audclk=%.3f\n",
               pts, last_serial, serial, get_clock(&is->audclk));
        last_serial = serial;
    } else if (!isnan(pts) && !isnan(last_pts) && fabs(pts - last_pts) > 1.0) {
        /* PTS 跳变超过 1 秒 */
        av_log(NULL, AV_LOG_WARNING, 
               "[VIDEO-QUEUE] PTS jump: %.3f -> %.3f (delta=%.3f)\n",
               last_pts, pts, pts - last_pts);
    }
    last_pts = pts;

    set_default_window_size(ffp, vp->width, vp->height, vp->sar);

    av_frame_move_ref(vp->frame, src_frame);
    frame_queue_push(&is->pictq);
    return 0;
}

int get_video_frame(FFPlayer *ffp, VideoState *is, AVFrame *frame)
{
    int got_picture;

    if ((got_picture = decoder_decode_frame(&is->viddec, frame, NULL, ffp->decoder_reorder_pts)) < 0)
        return -1;

    if (got_picture) {
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(is->video_st->time_base) * frame->pts;

        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

        if (ffp->framedrop>0 || (ffp->framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock(is);
                /*
                 * 倍速播放时的丢帧判断：
                 * 当 playback_rate > 1.0 时，音频时钟推进更快，
                 * 但视频帧 PTS 不变，diff 会更容易 < 0。
                 * 为避免过度丢帧，使用更宽松的阈值或完全不丢帧。
                 */
                double drop_threshold = (ffp->playback_rate > 1.001f) ? 
                                        AV_NOSYNC_THRESHOLD * ffp->playback_rate : 
                                        AV_NOSYNC_THRESHOLD;
                if (!isnan(diff) && fabs(diff) < drop_threshold &&
                    diff - is->frame_last_filter_delay < 0 &&
                    is->viddec.pkt_serial == is->vidclk.serial &&
                    is->videoq.nb_packets) {
                    is->frame_drops_early++;
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

#if CONFIG_AVFILTER
int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph) {
        outputs = avfilter_inout_alloc();
        inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name        = av_strdup("out");
        inputs->filter_ctx  = sink_ctx;
        inputs->pad_idx     = 0;
        inputs->next        = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

int configure_video_filters(FFPlayer *ffp, AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame)
{
    /* OpenGL 支持的像素格式 - 优先 YUV420P，其次 BGRA */
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_NONE
    };
    char sws_flags_str[512] = "";
    char buffersrc_args[256];
    int ret;
    AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
    AVCodecParameters *codecpar = is->video_st->codecpar;
    AVRational fr = av_guess_frame_rate(is->ic, is->video_st, NULL);
    const AVDictionaryEntry *e = NULL;
    
    (void)ffp; /* FFP 不再需要 renderer_info */

    while ((e = av_dict_get(ffp->sws_dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (!strcmp(e->key, "sws_flags")) {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        } else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str)-1] = '\0';

    graph->scale_sws_opts = av_strdup(sws_flags_str);

    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             is->video_st->time_base.num, is->video_st->time_base.den,
             codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1));
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0)
        goto fail;

    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0)
        goto fail;

    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts,  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    last_filter = filt_out;

/* Note: this macro adds a filter before the lastly added filter, so the
 * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg) do {                                          \
    AVFilterContext *filt_ctx;                                               \
                                                                             \
    ret = avfilter_graph_create_filter(&filt_ctx,                            \
                                       avfilter_get_by_name(name),           \
                                       "ffplay_" name, arg, NULL, graph);    \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    ret = avfilter_link(filt_ctx, 0, last_filter, 0);                        \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    last_filter = filt_ctx;                                                  \
} while (0)

    if (ffp->autorotate) {
        int32_t *displaymatrix = (int32_t *)av_stream_get_side_data(is->video_st, AV_PKT_DATA_DISPLAYMATRIX, NULL);
        double theta = get_rotation(displaymatrix);

        if (fabs(theta - 90) < 1.0) {
            INSERT_FILT("transpose", "clock");
        } else if (fabs(theta - 180) < 1.0) {
            INSERT_FILT("hflip", NULL);
            INSERT_FILT("vflip", NULL);
        } else if (fabs(theta - 270) < 1.0) {
            INSERT_FILT("transpose", "cclock");
        } else if (fabs(theta) > 1.0) {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            INSERT_FILT("rotate", rotate_buf);
        }
    }

    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
        goto fail;

    is->in_video_filter  = filt_src;
    is->out_video_filter = filt_out;

fail:
    return ret;
}

int configure_audio_filters(FFPlayer *ffp, VideoState *is, const char *afilters, int force_output_format)
{
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    int sample_rates[2] = { 0, -1 };
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    const AVDictionaryEntry *e = NULL;
    AVBPrint bp;
    char asrc_args[256];
    char afilters_args[512] = "";
    int ret;

    avfilter_graph_free(&is->agraph);
    if (!(is->agraph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);
    is->agraph->nb_threads = ffp->filter_nbthreads;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

    while ((e = av_dict_get(ffp->swr_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts)-1] = '\0';
    av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    av_channel_layout_describe_bprint(&is->audio_filter_src.ch_layout, &bp);

    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:time_base=%d/%d:channel_layout=%s",
                   is->audio_filter_src.freq, av_get_sample_fmt_name(is->audio_filter_src.fmt),
                   1, is->audio_filter_src.freq, bp.str);

    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, is->agraph);
    if (ret < 0)
        goto end;


    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, is->agraph);
    if (ret < 0)
        goto end;

    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (force_output_format) {
        sample_rates   [0] = is->audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set(filt_asink, "ch_layouts", bp.str, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates"   , sample_rates   ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }

    /* 构建滤镜字符串：先添加用户指定的滤镜 */
    if (afilters && afilters[0]) {
        av_strlcat(afilters_args, afilters, sizeof(afilters_args));
    }

    /* 添加倍速滤镜 atempo（范围 0.5 ~ 2.0）*/
    if (ffp->playback_rate > 0.001f && fabsf(ffp->playback_rate - 1.0f) > 0.001f) {
        float rate = ffp->playback_rate;
        /* 限制范围 */
        if (rate < 0.5f) rate = 0.5f;
        if (rate > 2.0f) rate = 2.0f;
        
        if (afilters_args[0])
            av_strlcat(afilters_args, ",", sizeof(afilters_args));
        
        av_strlcatf(afilters_args, sizeof(afilters_args), "atempo=%.2f", rate);
        av_log(NULL, AV_LOG_INFO, "[AudioFilter] Adding atempo filter: rate=%.2f\n", rate);
    }

    if ((ret = configure_filtergraph(is->agraph, afilters_args[0] ? afilters_args : NULL, filt_asrc, filt_asink)) < 0)
        goto end;

    is->in_audio_filter  = filt_asrc;
    is->out_audio_filter = filt_asink;

end:
    if (ret < 0)
        avfilter_graph_free(&is->agraph);
    av_bprint_finalize(&bp, NULL);

    return ret;
}
#endif  /* CONFIG_AVFILTER */

int audio_thread(void *arg)
{
    VideoState *is = arg;
    FFPlayer *ffp = is->ffp;
    AVFrame *frame = av_frame_alloc();
    Frame *af;
#if CONFIG_AVFILTER
    int last_serial = -1;
    int reconfigure;
    float last_playback_rate = 1.0f;  /* 记录上次的播放速率 */
#endif
    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        if ((got_frame = decoder_decode_frame(&is->auddec, frame, NULL, ffp->decoder_reorder_pts)) < 0)
            goto the_end;

        if (got_frame) {
                tb = (AVRational){1, frame->sample_rate};

#if CONFIG_AVFILTER
                /* 检测是否需要重配置滤镜：格式变化、序列号变化、或倍速变化 */
                reconfigure =
                    cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.ch_layout.nb_channels,
                                   frame->format, frame->ch_layout.nb_channels)    ||
                    av_channel_layout_compare(&is->audio_filter_src.ch_layout, &frame->ch_layout) ||
                    is->audio_filter_src.freq           != frame->sample_rate ||
                    is->auddec.pkt_serial               != last_serial ||
                    ffp->playback_rate_changed;

                /* 
                 * 保存旧的 playback_rate，用于 flush 时标记帧
                 * 必须在更新 last_playback_rate 之前保存！
                 */
                float flush_playback_rate = last_playback_rate;

                /* 检测倍速变化 */
                if (ffp->playback_rate_changed) {
                    av_log(NULL, AV_LOG_INFO, "[AudioThread] Playback rate changed: %.2f -> %.2f, reconfiguring filters\n",
                           last_playback_rate, ffp->playback_rate);
                    av_log(NULL, AV_LOG_INFO, "[AudioThread] Before reconfig: input_frame_pts=%.3f audclk=%.3f sampq_size=%d\n",
                           frame->pts * av_q2d(tb), is->audio_clock, frame_queue_nb_remaining(&is->sampq));
                    ffp->playback_rate_changed = 0;
                    last_playback_rate = ffp->playback_rate;
                }

                if (reconfigure) {
                    char buf1[1024], buf2[1024];
                    av_channel_layout_describe(&is->audio_filter_src.ch_layout, buf1, sizeof(buf1));
                    av_channel_layout_describe(&frame->ch_layout, buf2, sizeof(buf2));
                    av_log(NULL, AV_LOG_DEBUG,
                           "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                           is->audio_filter_src.freq, is->audio_filter_src.ch_layout.nb_channels, av_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
                           frame->sample_rate, frame->ch_layout.nb_channels, av_get_sample_fmt_name(frame->format), buf2, is->auddec.pkt_serial);

                    /* 
                     * 在销毁旧滤镜前，flush 滤镜获取所有缓冲的输出
                     * 这样可以避免 atempo 滤镜内部缓冲数据丢失导致的 PTS 跳变
                     */
                    if (is->in_audio_filter && is->out_audio_filter) {
                        AVFrame *flush_frame = av_frame_alloc();
                        if (flush_frame) {
                            /* 发送 NULL 帧触发 flush */
                            av_buffersrc_add_frame(is->in_audio_filter, NULL);
                            
                            /* 获取所有缓冲的输出帧 */
                            AVRational flush_tb = av_buffersink_get_time_base(is->out_audio_filter);
                            while (av_buffersink_get_frame_flags(is->out_audio_filter, flush_frame, 0) >= 0) {
                                Frame *flush_af = frame_queue_peek_writable(&is->sampq);
                                if (flush_af) {
                                    flush_af->pts = (flush_frame->pts == AV_NOPTS_VALUE) ? NAN : flush_frame->pts * av_q2d(flush_tb);
                                    flush_af->pos = flush_frame->pkt_pos;
                                    flush_af->serial = is->auddec.pkt_serial;
                                    flush_af->duration = av_q2d((AVRational){flush_frame->nb_samples, flush_frame->sample_rate});
                                    flush_af->playback_rate = flush_playback_rate;  // 使用旧的倍速！
                                    av_frame_move_ref(flush_af->frame, flush_frame);
                                    frame_queue_push(&is->sampq);
                                    av_log(NULL, AV_LOG_INFO, "[AudioFilter] Flush output: pts=%.3f rate=%.2f\n", flush_af->pts, flush_af->playback_rate);
                                } else {
                                    av_frame_unref(flush_frame);
                                    break;
                                }
                            }
                            av_frame_free(&flush_frame);
                        }
                    }

                    is->audio_filter_src.fmt            = frame->format;
                    ret = av_channel_layout_copy(&is->audio_filter_src.ch_layout, &frame->ch_layout);
                    if (ret < 0)
                        goto the_end;
                    is->audio_filter_src.freq           = frame->sample_rate;
                    last_serial                         = is->auddec.pkt_serial;

                    if ((ret = configure_audio_filters(ffp, is, ffp->afilters, 1)) < 0)
                        goto the_end;
                }

            if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                goto the_end;

            while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0) {
                tb = av_buffersink_get_time_base(is->out_audio_filter);
#endif
                if (!(af = frame_queue_peek_writable(&is->sampq)))
                    goto the_end;

                af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                af->pos = frame->pkt_pos;
                af->serial = is->auddec.pkt_serial;
                af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate});
                af->playback_rate = ffp->playback_rate;  // 记录该帧对应的播放倍速

                /* 日志：检测 seek 后第一帧和定期追踪 */
                {
                    static int last_audio_serial = -1;
                    static double last_output_pts = 0;
                    static int output_log_counter = 0;
                    
                    /* seek 后第一帧（serial 变化） */
                    if (af->serial != last_audio_serial) {
                        double media_pts = af->pts * af->playback_rate;
                        av_log(NULL, AV_LOG_INFO, 
                               "[AUDIO-QUEUE] First frame after seek: af_pts=%.3f media_pts=%.3f "
                               "serial=%d->%d rate=%.2f vidclk=%.3f\n",
                               af->pts, media_pts, last_audio_serial, af->serial,
                               af->playback_rate, get_clock(&is->vidclk));
                        last_audio_serial = af->serial;
                    }
                    
                    /* 定期打印状态 */
                    if (++output_log_counter >= 200) {  // 约每4秒打印一次
                        output_log_counter = 0;
                        double media_pts = af->pts * af->playback_rate;
                        av_log(NULL, AV_LOG_INFO, 
                               "[AUDIO] af_pts=%.3f media_pts=%.3f delta=%.3f rate=%.2f\n",
                               af->pts, media_pts, af->pts - last_output_pts, af->playback_rate);
                    }
                    last_output_pts = af->pts;
                }

                av_frame_move_ref(af->frame, frame);
                frame_queue_push(&is->sampq);

#if CONFIG_AVFILTER
                if (is->audioq.serial != is->auddec.pkt_serial)
                    break;
            }
            if (ret == AVERROR_EOF)
                is->auddec.finished = is->auddec.pkt_serial;
#endif
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
 the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&is->agraph);
#endif
    av_frame_free(&frame);
    return ret;
}

int video_thread(void *arg)
{
    VideoState *is = arg;
    FFPlayer *ffp = is->ffp;
    AVFrame *frame = av_frame_alloc();
    AVFrame *sw_frame = NULL;  /* 用于硬件帧转换 */
    double pts;
    double duration;
    int ret;
    AVRational tb = is->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

#if CONFIG_AVFILTER
    AVFilterGraph *graph = NULL;
    AVFilterContext *filt_out = NULL, *filt_in = NULL;
    int last_w = 0;
    int last_h = 0;
    enum AVPixelFormat last_format = -2;
    int last_serial = -1;
    int last_vfilter_idx = 0;
#endif

    if (!frame)
        return AVERROR(ENOMEM);

    int first_frame_logged = 0;
    
    for (;;) {
        ret = get_video_frame(ffp, is, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;

        /* 首帧日志：显示解码帧格式，帮助诊断 */
        if (!first_frame_logged) {
            av_log(NULL, AV_LOG_INFO, 
                   "[VideoThread] First frame: format=%s (%d), hw_pix_fmt=%s (%d), hwaccel_failed=%d\n",
                   av_get_pix_fmt_name(frame->format), frame->format,
                   av_get_pix_fmt_name(ffp->hw_pix_fmt), ffp->hw_pix_fmt,
                   ffp->hwaccel_failed);
            first_frame_logged = 1;
        }

        /* 
         * 硬件帧处理：
         * 如果帧是硬件格式（存储在 GPU 内存中），需要转换到 CPU 内存
         */
        AVFrame *display_frame = frame;
        if (frame->format == ffp->hw_pix_fmt && !ffp->hwaccel_failed) {
            if (!sw_frame) {
                sw_frame = av_frame_alloc();
                if (!sw_frame) {
                    av_log(NULL, AV_LOG_ERROR, "[HWAccel] Failed to allocate sw_frame\n");
                    ret = AVERROR(ENOMEM);
                    goto the_end;
                }
            }

            ret = hw_frame_to_sw(frame, sw_frame);
            if (ret < 0) {
                av_log(NULL, AV_LOG_WARNING, 
                       "[HWAccel] Frame transfer failed, switching to software decoding\n");
                ffp->hwaccel_failed = 1;
                /* 继续使用原始帧（可能无法正确显示）*/
            } else {
                display_frame = sw_frame;
                /* 
                 * 关键修复：立即释放硬件帧引用！
                 * hw_frame_to_sw 已将数据复制到 sw_frame，原始的 frame 仍然持有
                 * D3D11VA 表面的引用。如果不释放，当 av_buffersink_get_frame_flags
                 * 覆写 frame 时，硬件表面引用会泄漏，最终导致 
                 * "Static surface pool size exceeded" 错误。
                 */
                av_frame_unref(frame);
            }
        }

#if CONFIG_AVFILTER
        if (   last_w != display_frame->width
            || last_h != display_frame->height
            || last_format != display_frame->format
            || last_serial != is->viddec.pkt_serial
            || last_vfilter_idx != is->vfilter_idx) {
            av_log(NULL, AV_LOG_DEBUG,
                   "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                   last_w, last_h,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                   display_frame->width, display_frame->height,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(display_frame->format), "none"), is->viddec.pkt_serial);
            avfilter_graph_free(&graph);
            graph = avfilter_graph_alloc();
            if (!graph) {
                ret = AVERROR(ENOMEM);
                goto the_end;
            }
            graph->nb_threads = ffp->filter_nbthreads;
            if ((ret = configure_video_filters(ffp, graph, is, ffp->vfilters_list ? ffp->vfilters_list[is->vfilter_idx] : NULL, display_frame)) < 0) {
                SDL_Event event;
                event.type = FF_QUIT_EVENT;
                event.user.data1 = is;
                SDL_PushEvent(&event);
                goto the_end;
            }
            filt_in  = is->in_video_filter;
            filt_out = is->out_video_filter;
            last_w = display_frame->width;
            last_h = display_frame->height;
            last_format = display_frame->format;
            last_serial = is->viddec.pkt_serial;
            last_vfilter_idx = is->vfilter_idx;
            frame_rate = av_buffersink_get_frame_rate(filt_out);
        }

        ret = av_buffersrc_add_frame(filt_in, display_frame);
        
        /* 清理转换后的软件帧 */
        if (display_frame == sw_frame) {
            av_frame_unref(sw_frame);
        }
        if (ret < 0)
            goto the_end;

        while (ret >= 0) {
            is->frame_last_returned_time = av_gettime_relative() / 1000000.0;

            ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
            if (ret < 0) {
                if (ret == AVERROR_EOF)
                    is->viddec.finished = is->viddec.pkt_serial;
                ret = 0;
                break;
            }

            is->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;
            if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                is->frame_last_filter_delay = 0;
            tb = av_buffersink_get_time_base(filt_out);
#endif
            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            ret = queue_picture(ffp, is, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
            av_frame_unref(frame);
#if CONFIG_AVFILTER
            if (is->videoq.serial != is->viddec.pkt_serial)
                break;
        }
#endif

        if (ret < 0)
            goto the_end;
    }
 the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&graph);
#endif
    av_frame_free(&sw_frame);  /* 清理硬件帧转换用的软件帧 */
    av_frame_free(&frame);
    return 0;
}

int subtitle_thread(void *arg)
{
    VideoState *is = arg;
    FFPlayer *ffp = is->ffp;
    Frame *sp;
    int got_subtitle;
    double pts;

    for (;;) {
        if (!(sp = frame_queue_peek_writable(&is->subpq)))
            return 0;

        if ((got_subtitle = decoder_decode_frame(&is->subdec, NULL, &sp->sub, ffp->decoder_reorder_pts)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = is->subdec.pkt_serial;
            sp->width = is->subdec.avctx->width;
            sp->height = is->subdec.avctx->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            frame_queue_push(&is->subpq);
        } else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}

/* copy samples for viewing in editor window */
void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
int synchronize_audio(FFPlayer *ffp, VideoState *is, int nb_samples)
{
    (void)ffp; /* unused for now */
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = get_clock(&is->audclk) - get_master_clock(is);

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                        diff, avg_diff, wanted_nb_samples - nb_samples,
                        is->audio_clock, is->audio_diff_threshold);
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}

/**
 * Decode one audio frame and return its uncompressed size.
 */
int audio_decode_frame(FFPlayer *ffp, VideoState *is)
{
    int data_size, resampled_data_size;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    if (is->paused)
        return -1;

    do {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&is->sampq) == 0) {
            if ((av_gettime_relative() - ffp->audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        if (!(af = frame_queue_peek_readable(&is->sampq)))
            return -1;
        frame_queue_next(&is->sampq);
    } while (af->serial != is->audioq.serial);

    data_size = av_samples_get_buffer_size(NULL, af->frame->ch_layout.nb_channels,
                                           af->frame->nb_samples,
                                           af->frame->format, 1);
    wanted_nb_samples = synchronize_audio(ffp, is, af->frame->nb_samples);

    if (af->frame->format        != is->audio_src.fmt            ||
        av_channel_layout_compare(&af->frame->ch_layout, &is->audio_src.ch_layout) ||
        af->frame->sample_rate   != is->audio_src.freq           ||
        (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx)) {
        swr_free(&is->swr_ctx);
        swr_alloc_set_opts2(&is->swr_ctx,
                            &is->audio_tgt.ch_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
                            &af->frame->ch_layout, af->frame->format, af->frame->sample_rate,
                            0, NULL);
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), af->frame->ch_layout.nb_channels,
                    is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.ch_layout.nb_channels);
            swr_free(&is->swr_ctx);
            return -1;
        }
        if (av_channel_layout_copy(&is->audio_src.ch_layout, &af->frame->ch_layout) < 0)
            return -1;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = af->frame->format;
    }

    if (is->swr_ctx) {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        uint8_t **out = &is->audio_buf1;
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
        int out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.ch_layout.nb_channels, out_count, is->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
                                        wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * is->audio_tgt.ch_layout.nb_channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    } else {
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts)) {
        /*
         * 重要：atempo 滤镜输出的 PTS 是"播放时间"而非"媒体时间"
         * 例如：0.5倍速时，原始5秒音频被拉伸成10秒播放，atempo输出PTS是0-10
         * 但视频的PTS仍然是原始媒体时间（0-5秒对应原始内容）
         * 
         * 为了正确同步，需要将播放时间转换回媒体时间：
         * 媒体时间 = 播放时间 × playback_rate
         * 
         * 关键：必须使用该帧入队时的 playback_rate（af->playback_rate），
         * 而不是当前的 ffp->playback_rate，否则在倍速切换时会导致时钟跳变
         */
        double media_pts = af->pts;
        double samples_duration = (double)af->frame->nb_samples / af->frame->sample_rate;
        float frame_rate = af->playback_rate;
        
        if (frame_rate > 0.001f && fabsf(frame_rate - 1.0f) > 0.001f) {
            media_pts = af->pts * frame_rate;
            samples_duration = samples_duration * frame_rate;
        }
        is->audio_clock = media_pts + samples_duration;
    } else {
        is->audio_clock = NAN;
    }
    is->audio_clock_serial = af->serial;
    
    /* 检测音频时钟跳变（可能由 seek 或倍速切换引起） */
    if (!isnan(audio_clock0) && !isnan(is->audio_clock)) {
        double clock_delta = is->audio_clock - audio_clock0;
        /* 如果时钟跳变超过 0.5 秒或回退，记录详细日志 */
        if (fabs(clock_delta) > 0.5 || clock_delta < -0.1) {
            double vidclk = get_clock(&is->vidclk);
            av_log(NULL, AV_LOG_WARNING, 
                   "[CLOCK-JUMP] audio: %.3f->%.3f (delta=%.3f) vidclk=%.3f "
                   "af_pts=%.3f af_rate=%.2f serial=%d\n",
                   audio_clock0, is->audio_clock, clock_delta, vidclk,
                   af->pts, af->playback_rate, af->serial);
        }
    }
    
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->audio_clock - last_clock,
               is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}

/* 音频馈送函数 - 将解码的音频数据写入混音器流
 * 这个函数从音频馈送线程中调用 */
static void audio_feed_stream(FFPlayer *ffp, VideoState *is)
{
    if (!is || !is->mixer_stream || !is->audio_callback_enabled) {
        return;
    }
    
    if (is->abort_request) {
        return;
    }
    
    AudioStream *stream = is->mixer_stream;
    
    /* 检查流的可写空间 */
    int free_space = audio_stream_get_free_space(stream);
    if (free_space < 4096) {
        /* 缓冲区接近满，等待消费 */
        return;
    }
    
    ffp->audio_callback_time = av_gettime_relative();
    
    /* 解码并填充数据 */
    int bytes_to_fill = FFMIN(free_space, 8192);  /* 每次最多填充 8KB */
    int bytes_filled = 0;
    
    while (bytes_filled < bytes_to_fill && !is->abort_request && is->audio_callback_enabled) {
        if (is->audio_buf_index >= is->audio_buf_size) {
            int audio_size = audio_decode_frame(ffp, is);
            if (audio_size < 0) {
                /* 解码失败或没有数据，退出 */
                break;
            } else {
                if (is->show_mode != SHOW_MODE_VIDEO)
                    update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }
        
        int len1 = is->audio_buf_size - is->audio_buf_index;
        int to_write = FFMIN(len1, bytes_to_fill - bytes_filled);
        
        if (to_write > 0 && is->audio_buf) {
            /* 直接写入原始音频数据，音量由混音器统一处理
             * 注意：不在这里处理音量，避免双重调节导致的噪声 */
            if (is->muted) {
                /* 静音时写入静音数据 */
                uint8_t silence[8192] = {0};
                audio_stream_write(stream, silence, to_write);
            } else {
                /* 写入原始数据，混音器会根据 stream->volume 调节 */
                audio_stream_write(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, to_write);
            }
            bytes_filled += to_write;
            is->audio_buf_index += to_write;
        }
    }
    
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    
    /* 更新音频时钟 */
    if (!isnan(is->audio_clock)) {
        int queued = audio_stream_get_queued(stream);
        set_clock_at(&is->audclk, 
                     is->audio_clock - (double)(is->audio_hw_buf_size + queued + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, 
                     is->audio_clock_serial, 
                     ffp->audio_callback_time / 1000000.0);
        sync_clock_to_slave(&is->extclk, &is->audclk);
    }
}

/* 音频馈送线程 - 持续将解码的音频数据推送到混音器 */
static int audio_feeder_thread(void *arg)
{
    VideoState *is = arg;
    FFPlayer *ffp = is->ffp;
    
    av_log(NULL, AV_LOG_INFO, "[AUDIO-FEEDER] Thread started for is=%p\n", is);
    
    while (!is->abort_request && is->audio_callback_enabled) {
        audio_feed_stream(ffp, is);
        
        /* 短暂休眠，避免 CPU 过载 */
        SDL_Delay(5);
    }
    
    av_log(NULL, AV_LOG_INFO, "[AUDIO-FEEDER] Thread exiting for is=%p\n", is);
    return 0;
}

int audio_open(FFPlayer *ffp, void *opaque, AVChannelLayout *wanted_channel_layout, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    VideoState *is = opaque;
    const char *env;
    int wanted_nb_channels = wanted_channel_layout->nb_channels;
    int sample_rate = wanted_sample_rate;
    int samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_sample_rate / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        av_channel_layout_uninit(wanted_channel_layout);
        av_channel_layout_default(wanted_channel_layout, wanted_nb_channels);
    }
    if (wanted_channel_layout->order != AV_CHANNEL_ORDER_NATIVE) {
        av_channel_layout_uninit(wanted_channel_layout);
        av_channel_layout_default(wanted_channel_layout, wanted_nb_channels);
    }
    wanted_nb_channels = wanted_channel_layout->nb_channels;
    
    if (sample_rate <= 0 || wanted_nb_channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    
    av_log(NULL, AV_LOG_INFO, "[AUDIO-OPEN] ffp=%p is=%p Using audio mixer\n", ffp, is);
    
    /* 获取或创建全局混音器 */
    AudioMixer *mixer = audio_mixer_get_global();
    if (!mixer) {
        mixer = audio_mixer_create(sample_rate, wanted_nb_channels, samples);
        if (!mixer) {
            av_log(NULL, AV_LOG_ERROR, "[AUDIO-OPEN] Failed to create audio mixer\n");
            return -1;
        }
    }
    
    /* 为此播放器创建音频流 */
    AudioStream *stream = audio_mixer_add_stream(mixer, is);
    if (!stream) {
        av_log(NULL, AV_LOG_ERROR, "[AUDIO-OPEN] Failed to add audio stream to mixer\n");
        return -1;
    }
    
    /* 保存流句柄 */
    is->mixer_stream = stream;
    
    /* 设置初始音量（同步 is->audio_volume 到混音器流）*/
    audio_stream_set_volume(stream, is->audio_volume);
    
    /* 使用混音器的实际参数 */
    int mixer_rate = audio_mixer_get_sample_rate(mixer);
    int mixer_channels = audio_mixer_get_channels(mixer);
    
    if (mixer_rate > 0) sample_rate = mixer_rate;
    if (mixer_channels > 0) {
        wanted_nb_channels = mixer_channels;
        av_channel_layout_uninit(wanted_channel_layout);
        av_channel_layout_default(wanted_channel_layout, wanted_nb_channels);
    }
    
    /* 设置音频硬件参数 */
    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = sample_rate;
    if (av_channel_layout_copy(&audio_hw_params->ch_layout, wanted_channel_layout) < 0)
        return -1;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->ch_layout.nb_channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->ch_layout.nb_channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        audio_mixer_remove_stream(mixer, stream);
        is->mixer_stream = NULL;
        return -1;
    }
    
    /* 启动音频流 */
    audio_stream_play(stream);
    
    av_log(NULL, AV_LOG_INFO, "[AUDIO-OPEN] ffp=%p is=%p stream=%p opened successfully (rate=%d, channels=%d)\n",
           ffp, is, stream, sample_rate, wanted_nb_channels);
    
    /* 返回一个合理的缓冲区大小 */
    return samples * wanted_nb_channels * sizeof(int16_t);
}

/* open a given stream. Return 0 if OK */
int stream_component_open(FFPlayer *ffp, VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;
    const AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts = NULL;
    const AVDictionaryEntry *t = NULL;
    int sample_rate;
    AVChannelLayout ch_layout = { 0 };
    int ret = 0;
    int stream_lowres = ffp->lowres;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    codec = avcodec_find_decoder(avctx->codec_id);

    switch(avctx->codec_type){
        case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; forced_codec_name = ffp->audio_codec_name; break;
        case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; forced_codec_name = ffp->subtitle_codec_name; break;
        case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; forced_codec_name = ffp->video_codec_name; break;
    }
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with name '%s'\n", forced_codec_name);
        else                   av_log(NULL, AV_LOG_WARNING,
                                      "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                codec->max_lowres);
        stream_lowres = codec->max_lowres;
    }
    avctx->lowres = stream_lowres;

    if (ffp->fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;

    /* 为视频解码器配置硬件加速 */
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        av_log(NULL, AV_LOG_INFO, "[HWAccel] Attempting to configure hwaccel, type=%d (%s)\n",
               ffp->hwaccel_type, ffp_get_hwaccel_name(ffp->hwaccel_type));
        int hwaccel_ret = configure_hwaccel(ffp, avctx, codec);
        av_log(NULL, AV_LOG_INFO, "[HWAccel] Configure result: %s, hw_pix_fmt=%d, hwaccel_failed=%d\n",
               hwaccel_ret ? "SUCCESS" : "FALLBACK_TO_SW", ffp->hw_pix_fmt, ffp->hwaccel_failed);
    }

    opts = filter_codec_opts(ffp->codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret =  AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    is->eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER
        {
            AVFilterContext *sink;

            is->audio_filter_src.freq           = avctx->sample_rate;
            ret = av_channel_layout_copy(&is->audio_filter_src.ch_layout, &avctx->ch_layout);
            if (ret < 0)
                goto fail;
            is->audio_filter_src.fmt            = avctx->sample_fmt;
            if ((ret = configure_audio_filters(ffp, is, ffp->afilters, 0)) < 0)
                goto fail;
            sink = is->out_audio_filter;
            sample_rate    = av_buffersink_get_sample_rate(sink);
            ret = av_buffersink_get_ch_layout(sink, &ch_layout);
            if (ret < 0)
                goto fail;
        }
#else
        sample_rate    = avctx->sample_rate;
        ret = av_channel_layout_copy(&ch_layout, &avctx->ch_layout);
        if (ret < 0)
            goto fail;
#endif

        /* prepare audio output */
        if ((ret = audio_open(ffp, is, &ch_layout, sample_rate, &is->audio_tgt)) < 0)
            goto fail;
        is->audio_hw_buf_size = ret;
        is->audio_src = is->audio_tgt;
        is->audio_buf_size  = 0;
        is->audio_buf_index = 0;

        /* init averaging filter */
        is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];

        if ((ret = decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread)) < 0)
            goto fail;
        if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
            is->auddec.start_pts = is->audio_st->start_time;
            is->auddec.start_pts_tb = is->audio_st->time_base;
        }
        if ((ret = decoder_start(&is->auddec, audio_thread, "audio_decoder", is)) < 0)
            goto out;
        is->audio_callback_enabled = 1;  /* 启用音频回调 */
        
        /* 启动音频馈送线程 */
        is->audio_feeder_tid = SDL_CreateThread(audio_feeder_thread, "audio_feeder", is);
        if (!is->audio_feeder_tid) {
            av_log(NULL, AV_LOG_ERROR, "Failed to create audio feeder thread\n");
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];

        if ((ret = decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread)) < 0)
            goto fail;
        if ((ret = decoder_start(&is->viddec, video_thread, "video_decoder", is)) < 0)
            goto out;
        is->queue_attachments_req = 1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_stream = stream_index;
        is->subtitle_st = ic->streams[stream_index];

        if ((ret = decoder_init(&is->subdec, avctx, &is->subtitleq, is->continue_read_thread)) < 0)
            goto fail;
        if ((ret = decoder_start(&is->subdec, subtitle_thread, "subtitle_decoder", is)) < 0)
            goto out;
        break;
    default:
        break;
    }
    goto out;

fail:
    fprintf(stderr, "[stream_component_open] Failed with ret=%d for stream_index=%d\n", ret, stream_index);
    fflush(stderr);
    avcodec_free_context(&avctx);
out:
    av_channel_layout_uninit(&ch_layout);
    av_dict_free(&opts);

    return ret;
}

int decode_interrupt_cb(void *ctx)
{
    VideoState *is = ctx;
    return is->abort_request;
}

int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue) {
    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}

/* 从 URL 检测是否为直播流（用于 avformat_open_input 之前）*/
int is_live_url(const char *url)
{
    if (!url)
        return 0;
    
    /* 基于 URL 协议检测 */
    if (!strncmp(url, "rtmp://", 7) ||
        !strncmp(url, "rtmps://", 8) ||
        !strncmp(url, "rtmpt://", 8) ||
        !strncmp(url, "rtsp://", 7) ||
        !strncmp(url, "rtsps://", 8) ||
        !strncmp(url, "rtp://", 6) ||
        !strncmp(url, "udp://", 6) ||
        !strncmp(url, "srt://", 6))
        return 1;
    
    /* 检测 HTTP-FLV (以 .flv 结尾或包含 /live/) */
    if (strstr(url, ".flv") && 
        (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)))
        return 1;
    
    /* 检测 HLS */
    if (strstr(url, ".m3u8") && 
        (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)))
        return 1;
    
    return 0;
}

int is_realtime(AVFormatContext *s)
{
    const char *name = s->iformat->name;
    
    /* 1. 基于格式名称检测 */
    if (!strcmp(name, "rtp") ||
        !strcmp(name, "rtsp") ||
        !strcmp(name, "sdp") ||
        !strcmp(name, "flv") ||      /* HTTP-FLV 直播 */
        !strcmp(name, "hls") ||      /* HLS 直播 */
        !strcmp(name, "dash"))       /* DASH 直播 */
        return 1;
    
    /* 2. 基于 URL 协议检测 */
    if (s->url && is_live_url(s->url))
        return 1;
    
    return 0;
}

/* 检测是否可 seek */
int is_seekable(AVFormatContext *s)
{
    /* 直播流不可 seek */
    if (is_realtime(s))
        return 0;
    
    /* 检查 pb 是否支持 seek */
    if (s->pb && !s->pb->seekable)
        return 0;
    
    /* 检查 duration 是否有效 */
    if (s->duration <= 0 || s->duration == AV_NOPTS_VALUE)
        return 0;
    
    return 1;
}

/* this thread gets the stream from the disk or the network */
int read_thread(void *arg)
{
    VideoState *is = arg;
    FFPlayer *ffp = is->ffp;
    AVFormatContext *ic = NULL;
    int err, i, ret;
    int st_index[AVMEDIA_TYPE_NB];
    AVPacket *pkt = NULL;
    int64_t stream_start_time;
    int pkt_in_play_range = 0;
    const AVDictionaryEntry *t;
    SDL_mutex *wait_mutex = SDL_CreateMutex();
    int scan_all_pmts_set = 0;
    int64_t pkt_ts;

    if (!wait_mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    memset(st_index, -1, sizeof(st_index));
    is->eof = 0;

    pkt = av_packet_alloc();
    if (!pkt) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate packet.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ic = avformat_alloc_context();
    if (!ic) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = is;
    if (!av_dict_get(ffp->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&ffp->format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }

    /* 在打开之前检测是否为直播流，并应用相应选项 */
    /* 注意：对于 RTMP 流暂时不应用 live options，因为某些选项可能不兼容 */
    if (is_live_url(is->filename)) {
        fprintf(stderr, "[LIVE] Detected live URL: %s\n", is->filename);
        fflush(stderr);
        
        /* RTMP 流不应用通用的 live options，只设置基本参数 */
        if (strncmp(is->filename, "rtmp://", 7) == 0 ||
            strncmp(is->filename, "rtmps://", 8) == 0 ||
            strncmp(is->filename, "rtmpt://", 8) == 0) {
            fprintf(stderr, "[LIVE] RTMP stream detected, using minimal options\n");
            fflush(stderr);
            /* 对 RTMP 只设置基本的探测参数 */
            av_dict_set(&ffp->format_opts, "analyzeduration", "2000000", 0);
            av_dict_set(&ffp->format_opts, "probesize", "2000000", 0);
        } else {
            ffp_apply_live_options(ffp);
        }
    }

    fprintf(stderr, "[READ_THREAD] Opening input: %s\n", is->filename);
    fflush(stderr);
    
    /* 打印所有 format_opts */
    {
        const AVDictionaryEntry *e = NULL;
        fprintf(stderr, "[READ_THREAD] format_opts:\n");
        while ((e = av_dict_get(ffp->format_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
            fprintf(stderr, "  %s = %s\n", e->key, e->value);
        }
        fflush(stderr);
    }
    
    err = avformat_open_input(&ic, is->filename, is->iformat, &ffp->format_opts);
    if (err < 0) {
        fprintf(stderr, "[READ_THREAD] avformat_open_input failed: %d\n", err);
        fflush(stderr);
        print_error(is->filename, err);
        ret = -1;
        goto fail;
    }
    fprintf(stderr, "[READ_THREAD] avformat_open_input succeeded\n");
    fflush(stderr);
    if (scan_all_pmts_set)
        av_dict_set(&ffp->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    /* 检查是否有未被 FFmpeg 消费的选项（即 FFmpeg 不认识的选项）*/
    /* 注意：改为警告而不是错误，因为某些通用选项（如 reconnect）不是所有协议都支持 */
    if ((t = av_dict_get(ffp->format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        fprintf(stderr, "[READ_THREAD] WARNING: Some options not recognized by this format:\n");
        const AVDictionaryEntry *e = NULL;
        while ((e = av_dict_get(ffp->format_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
            fprintf(stderr, "  - %s = %s (ignored)\n", e->key, e->value);
        }
        fflush(stderr);
        /* 不再因为未知选项而失败，只是警告 */
    }
    is->ic = ic;

    if (ffp->genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    av_format_inject_global_side_data(ic);

    if (ffp->find_stream_info) {
        AVDictionary **opts = setup_find_stream_info_opts(ic, ffp->codec_opts);
        int orig_nb_streams = ic->nb_streams;

        fprintf(stderr, "[READ_THREAD] Finding stream info...\n");
        fflush(stderr);
        err = avformat_find_stream_info(ic, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (err < 0) {
            fprintf(stderr, "[READ_THREAD] avformat_find_stream_info failed: %d\n", err);
            fflush(stderr);
            av_log(NULL, AV_LOG_WARNING,
                   "%s: could not find codec parameters\n", is->filename);
            ret = -1;
            goto fail;
        }
        fprintf(stderr, "[READ_THREAD] avformat_find_stream_info succeeded, nb_streams=%d\n", ic->nb_streams);
        fflush(stderr);
    }

    if (ic->pb)
        ic->pb->eof_reached = 0;

    if (ffp->seek_by_bytes < 0)
        ffp->seek_by_bytes = !(ic->iformat->flags & AVFMT_NO_BYTE_SEEK) &&
                        !!(ic->iformat->flags & AVFMT_TS_DISCONT) &&
                        strcmp("ogg", ic->iformat->name);

    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    if (!ffp->window_title && (t = av_dict_get(ic->metadata, "title", NULL, 0)))
        ffp->window_title = av_asprintf("%s - %s", t->value, ffp->input_filename);

    if (ffp->start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = ffp->start_time;
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                    is->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    is->realtime = is_realtime(ic);
    
    /* 更新 FFPlayer 的媒体类型信息 */
    ffp->is_realtime = is->realtime;
    ffp->is_seekable = is_seekable(ic);
    
    if (is->realtime) {
        ffp->media_type = FFP_MEDIA_TYPE_LIVE;
        av_log(NULL, AV_LOG_INFO, "[LIVE] Detected live stream: %s\n", is->filename);
    } else if (ffp->is_seekable) {
        /* 判断是本地文件还是网络点播 */
        if (is->filename && (strncmp(is->filename, "http://", 7) == 0 ||
                             strncmp(is->filename, "https://", 8) == 0 ||
                             strncmp(is->filename, "ftp://", 6) == 0)) {
            ffp->media_type = FFP_MEDIA_TYPE_VOD;
        } else {
            ffp->media_type = FFP_MEDIA_TYPE_FILE;
        }
    } else {
        ffp->media_type = FFP_MEDIA_TYPE_UNKNOWN;
    }
    
    /* 通知上层媒体类型 */
    ffp_notify_msg3(ffp, FFP_MSG_MEDIA_TYPE_CHANGED, ffp->media_type, ffp->is_seekable);

    if (ffp->show_status)
        av_dump_format(ic, 0, is->filename, 0);

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
        if (type >= 0 && ffp->wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, ffp->wanted_stream_spec[type]) > 0)
                st_index[type] = i;
    }
    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (ffp->wanted_stream_spec[i] && st_index[i] == -1) {
            av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", ffp->wanted_stream_spec[i], av_get_media_type_string(i));
            st_index[i] = INT_MAX;
        }
    }

    if (!ffp->video_disable)
        st_index[AVMEDIA_TYPE_VIDEO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    if (!ffp->audio_disable)
        st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
    if (!ffp->video_disable && !ffp->subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =
            av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                st_index[AVMEDIA_TYPE_SUBTITLE],
                                (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                 st_index[AVMEDIA_TYPE_AUDIO] :
                                 st_index[AVMEDIA_TYPE_VIDEO]),
                                NULL, 0);
    
    fprintf(stderr, "[READ_THREAD] Stream indices: video=%d, audio=%d, subtitle=%d\n",
           st_index[AVMEDIA_TYPE_VIDEO], st_index[AVMEDIA_TYPE_AUDIO], st_index[AVMEDIA_TYPE_SUBTITLE]);
    fflush(stderr);

    is->show_mode = ffp->show_mode;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecParameters *codecpar = st->codecpar;
        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
        if (codecpar->width)
            set_default_window_size(ffp, codecpar->width, codecpar->height, sar);
    }

    /* open the streams */
    fprintf(stderr, "[READ_THREAD] Opening streams...\n");
    fflush(stderr);
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        int audio_ret = stream_component_open(ffp, is, st_index[AVMEDIA_TYPE_AUDIO]);
        fprintf(stderr, "[READ_THREAD] Audio stream open result: %d, is->audio_stream=%d\n", 
               audio_ret, is->audio_stream);
        fflush(stderr);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret = stream_component_open(ffp, is, st_index[AVMEDIA_TYPE_VIDEO]);
        fprintf(stderr, "[READ_THREAD] Video stream open result: %d, is->video_stream=%d\n", 
               ret, is->video_stream);
        fflush(stderr);
    }
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        int sub_ret = stream_component_open(ffp, is, st_index[AVMEDIA_TYPE_SUBTITLE]);
        fprintf(stderr, "[READ_THREAD] Subtitle stream open result: %d\n", sub_ret);
        fflush(stderr);
    }

    fprintf(stderr, "[READ_THREAD] Final stream status: video_stream=%d, audio_stream=%d\n",
           is->video_stream, is->audio_stream);
    fflush(stderr);

    if (is->video_stream < 0 && is->audio_stream < 0) {
        fprintf(stderr, "[READ_THREAD] Failed to open file '%s' or configure filtergraph\n",
               is->filename);
        fflush(stderr);
        ret = -1;
        goto fail;
    }

    if (ffp->infinite_buffer < 0 && is->realtime)
        ffp->infinite_buffer = 1;

    /* 媒体流成功打开，发送 PREPARED 消息 */
    fprintf(stderr, "[READ_THREAD] Media prepared successfully, sending FFP_MSG_PREPARED\n");
    fflush(stderr);
    ffp->prepared = 1;
    ffp_notify_msg1(ffp, FFP_MSG_PREPARED);

    for (;;) {
        if (is->abort_request)
            break;
        if (is->paused != is->last_paused) {
            is->last_paused = is->paused;
            if (is->paused)
                is->read_pause_return = av_read_pause(ic);
            else
                av_read_play(ic);
        }
#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        if (is->paused &&
                (!strcmp(ic->iformat->name, "rtsp") ||
                 (ic->pb && !strncmp(ffp->input_filename, "mmsh:", 5)))) {
            SDL_Delay(10);
            continue;
        }
#endif
        if (is->seek_req) {
            int64_t seek_target = is->seek_pos;
            int64_t seek_min    = is->seek_rel > 0 ? seek_target - is->seek_rel + 2: INT64_MIN;
            int64_t seek_max    = is->seek_rel < 0 ? seek_target - is->seek_rel - 2: INT64_MAX;
            
            /* Seek 前的状态日志 */
            av_log(NULL, AV_LOG_INFO, 
                   "[SEEK] >>> Before: target=%.3f audclk=%.3f vidclk=%.3f audio_clock=%.3f rate=%.2f\n",
                   seek_target / (double)AV_TIME_BASE,
                   get_clock(&is->audclk), get_clock(&is->vidclk),
                   is->audio_clock, ffp->playback_rate);

            ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", is->ic->url);
            } else {
                if (is->audio_stream >= 0)
                    packet_queue_flush(&is->audioq);
                if (is->subtitle_stream >= 0)
                    packet_queue_flush(&is->subtitleq);
                if (is->video_stream >= 0)
                    packet_queue_flush(&is->videoq);
                if (is->seek_flags & AVSEEK_FLAG_BYTE) {
                   set_clock(&is->extclk, NAN, 0);
                } else {
                   set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
                }
                
                av_log(NULL, AV_LOG_INFO, 
                       "[SEEK] <<< After flush: extclk=%.3f sampq=%d pictq=%d\n",
                       get_clock(&is->extclk),
                       frame_queue_nb_remaining(&is->sampq),
                       frame_queue_nb_remaining(&is->pictq));
            }
            is->seek_req = 0;
            is->queue_attachments_req = 1;
            is->eof = 0;
            
            /* 发送 seek 完成消息，通知上层清除 seek_req 标志 */
            ffp_notify_msg3(ffp, FFP_MSG_SEEK_COMPLETE, 
                           (int)(seek_target / (AV_TIME_BASE / 1000)), ret);
            
            if (is->paused)
                step_to_next_frame(is);
        }
        if (is->queue_attachments_req) {
            if (is->video_st && is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                if ((ret = av_packet_ref(pkt, &is->video_st->attached_pic)) < 0)
                    goto fail;
                packet_queue_put(&is->videoq, pkt);
                packet_queue_put_nullpacket(&is->videoq, pkt, is->video_stream);
            }
            is->queue_attachments_req = 0;
        }

        if (ffp->infinite_buffer<1 &&
              (is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE
            || (stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq) &&
                stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq) &&
                stream_has_enough_packets(is->subtitle_st, is->subtitle_stream, &is->subtitleq)))) {
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        if (!is->paused &&
            (!is->audio_st || (is->auddec.finished == is->audioq.serial && frame_queue_nb_remaining(&is->sampq) == 0)) &&
            (!is->video_st || (is->viddec.finished == is->videoq.serial && frame_queue_nb_remaining(&is->pictq) == 0))) {
            /* 所有音视频帧已播放完毕 */
            if (ffp->loop != 1 && (!ffp->loop || --ffp->loop)) {
                /* 需要循环：seek 到开头继续播放 */
                stream_seek(is, ffp->start_time != AV_NOPTS_VALUE ? ffp->start_time : 0, 0, 0);
            } else {
                /* 不需要循环：播放结束，退出 read_thread */
                av_log(NULL, AV_LOG_INFO, "[READ_THREAD] Playback finished, exiting\n");
                break;  /* 跳出 for 循环，发送 FFP_MSG_COMPLETED */
            }
        }
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->videoq, pkt, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audioq, pkt, is->audio_stream);
                if (is->subtitle_stream >= 0)
                    packet_queue_put_nullpacket(&is->subtitleq, pkt, is->subtitle_stream);
                is->eof = 1;
            }
            if (ic->pb && ic->pb->error) {
                if (ffp->autoexit)
                    goto fail;
                else
                    break;
            }
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        } else {
            is->eof = 0;
        }
        stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        pkt_in_play_range = ffp->duration == AV_NOPTS_VALUE ||
                (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                av_q2d(ic->streams[pkt->stream_index]->time_base) -
                (double)(ffp->start_time != AV_NOPTS_VALUE ? ffp->start_time : 0) / 1000000
                <= ((double)ffp->duration / 1000000);
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream && pkt_in_play_range
                   && !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            packet_queue_put(&is->videoq, pkt);
        } else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
            packet_queue_put(&is->subtitleq, pkt);
        } else {
            av_packet_unref(pkt);
        }
    }

    ret = 0;
    /* 播放正常结束，发送 COMPLETED 消息 */
    fprintf(stderr, "[READ_THREAD] Playback completed normally\n");
    fflush(stderr);
    ffp_notify_msg1(ffp, FFP_MSG_COMPLETED);
    
 fail:
    fprintf(stderr, "[READ_THREAD] Entering fail label, ret=%d\n", ret);
    fflush(stderr);
    if (ic && !is->ic)
        avformat_close_input(&ic);

    av_packet_free(&pkt);
    if (ret != 0) {
        SDL_Event event;

        /* 发送错误消息 */
        fprintf(stderr, "[READ_THREAD] Sending FFP_MSG_ERROR with ret=%d\n", ret);
        fflush(stderr);
        ffp_notify_msg2(ffp, FFP_MSG_ERROR, ret);

        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    SDL_DestroyMutex(wait_mutex);
    return 0;
}

VideoState *stream_open(FFPlayer *ffp, const char *filename, const AVInputFormat *iformat)
{
    VideoState *is;

    fprintf(stderr, "[STREAM_OPEN] Starting stream_open for: %s\n", filename);
    fflush(stderr);
    
    is = av_mallocz(sizeof(VideoState));
    if (!is) {
        fprintf(stderr, "[STREAM_OPEN] Failed to allocate VideoState\n");
        fflush(stderr);
        return NULL;
    }
    is->ffp = ffp;  /* Store FFPlayer reference */
    fprintf(stderr, "[STREAM_OPEN] Created VideoState is=%p for ffp=%p\n", (void*)is, (void*)ffp);
    fflush(stderr);
    is->last_video_stream = is->video_stream = -1;
    is->last_audio_stream = is->audio_stream = -1;
    is->last_subtitle_stream = is->subtitle_stream = -1;
    is->filename = av_strdup(filename);
    if (!is->filename)
        goto fail;
    is->iformat = iformat;
    is->ytop    = 0;
    is->xleft   = 0;

    /* start video display */
    if (frame_queue_init(&is->pictq, &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        goto fail;
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    if (packet_queue_init(&is->videoq) < 0 ||
        packet_queue_init(&is->audioq) < 0 ||
        packet_queue_init(&is->subtitleq) < 0)
        goto fail;

    if (!(is->continue_read_thread = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        goto fail;
    }

    init_clock(&is->vidclk, &is->videoq.serial);
    init_clock(&is->audclk, &is->audioq.serial);
    init_clock(&is->extclk, &is->extclk.serial);
    is->audio_clock_serial = -1;
    if (ffp->startup_volume < 0)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", ffp->startup_volume);
    if (ffp->startup_volume > 100)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", ffp->startup_volume);
    ffp->startup_volume = av_clip(ffp->startup_volume, 0, 100);
    ffp->startup_volume = av_clip(SDL_MIX_MAXVOLUME * ffp->startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
    is->audio_volume = ffp->startup_volume;
    is->muted = 0;
    is->av_sync_type = ffp->av_sync_type;
    fprintf(stderr, "[STREAM_OPEN] Creating read_thread...\n");
    fflush(stderr);
    is->read_tid     = SDL_CreateThread(read_thread, "read_thread", is);
    if (!is->read_tid) {
        fprintf(stderr, "[STREAM_OPEN] SDL_CreateThread failed: %s\n", SDL_GetError());
        fflush(stderr);
fail:
        fprintf(stderr, "[STREAM_OPEN] stream_open failed, cleaning up\n");
        fflush(stderr);
        stream_close(ffp, is);
        return NULL;
    }
    fprintf(stderr, "[STREAM_OPEN] read_thread created successfully, tid=%p\n", (void*)is->read_tid);
    fflush(stderr);
    
    return is;
}

void stream_cycle_channel(FFPlayer *ffp, VideoState *is, int codec_type)
{
    AVFormatContext *ic = is->ic;
    int start_index, stream_index;
    int old_index;
    AVStream *st;
    AVProgram *p = NULL;
    int nb_streams = is->ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = is->last_video_stream;
        old_index = is->video_stream;
    } else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = is->last_audio_stream;
        old_index = is->audio_stream;
    } else {
        start_index = is->last_subtitle_stream;
        old_index = is->subtitle_stream;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
        p = av_find_program_from_stream(ic, NULL, is->video_stream);
        if (p) {
            nb_streams = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index = -1;
            stream_index = start_index;
        }
    }

    for (;;) {
        if (++stream_index >= nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                is->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codecpar->codec_type == codec_type) {
            /* check that parameters are OK */
            switch (codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (st->codecpar->sample_rate != 0 &&
                    st->codecpar->ch_layout.nb_channels != 0)
                    goto the_end;
                break;
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
 the_end:
    if (p && stream_index != -1)
        stream_index = p->stream_index[stream_index];
    av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
           av_get_media_type_string(codec_type),
           old_index,
           stream_index);

    stream_component_close(ffp, is, old_index);
    stream_component_open(ffp, is, stream_index);
}

void toggle_full_screen(FFPlayer *ffp, VideoState *is)
{
    (void)is; /* unused */
    ffp->is_full_screen = !ffp->is_full_screen;
    SDL_SetWindowFullscreen(ffp->window, ffp->is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void toggle_audio_display(VideoState *is)
{
    int next = is->show_mode;
    do {
        next = (next + 1) % SHOW_MODE_NB;
    } while (next != is->show_mode && (next == SHOW_MODE_VIDEO && !is->video_st || next != SHOW_MODE_VIDEO && !is->audio_st));
    if (is->show_mode != next) {
        is->force_refresh = 1;
        is->show_mode = next;
    }
}

/* Event loop helper - wait for event with video refresh */
void refresh_loop_wait_event(FFPlayer *ffp, SDL_Event *event)
{
    double remaining_time = 0.0;
    VideoState *is = ffp->is;

    SDL_PumpEvents();
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (!ffp->cursor_hidden && av_gettime_relative() - ffp->cursor_last_shown > CURSOR_HIDE_DELAY) {
            SDL_ShowCursor(0);
            ffp->cursor_hidden = 1;
        }
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh))
            video_refresh(ffp, is, &remaining_time);
        SDL_PumpEvents();
    }
}

/* Seek to chapter */
void seek_chapter(FFPlayer *ffp, int incr)
{
    VideoState *is = ffp->is;
    int64_t pos = get_master_clock(is) * AV_TIME_BASE;
    int i;

    if (!is->ic->nb_chapters)
        return;

    /* find the current chapter */
    for (i = 0; i < is->ic->nb_chapters; i++) {
        AVChapter *ch = is->ic->chapters[i];
        if (av_compare_ts(pos, AV_TIME_BASE_Q, ch->start, ch->time_base) < 0) {
            i--;
            break;
        }
    }

    i += incr;
    i = FFMAX(i, 0);
    if (i >= is->ic->nb_chapters)
        return;

    av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
    stream_seek(is, av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,
                                 AV_TIME_BASE_Q), 0, 0);
}

/* Main event loop implementation */
void do_event_loop(FFPlayer *ffp)
{
    SDL_Event event;
    double incr, pos, frac;
    VideoState *cur_stream = ffp->is;

    for (;;) {
        double x;
        refresh_loop_wait_event(ffp, &event);
        switch (event.type) {
        case SDL_KEYDOWN:
            if (ffp->exit_on_keydown || event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                return; /* Exit event loop */
            }
            if (!cur_stream->width)
                continue;
            switch (event.key.keysym.sym) {
            case SDLK_f:
                toggle_full_screen(ffp, cur_stream);
                cur_stream->force_refresh = 1;
                break;
            case SDLK_p:
            case SDLK_SPACE:
                toggle_pause(cur_stream);
                break;
            case SDLK_m:
                toggle_mute(cur_stream);
                break;
            case SDLK_KP_MULTIPLY:
            case SDLK_0:
                update_volume(cur_stream, 1, SDL_VOLUME_STEP);
                break;
            case SDLK_KP_DIVIDE:
            case SDLK_9:
                update_volume(cur_stream, -1, SDL_VOLUME_STEP);
                break;
            case SDLK_s:
                step_to_next_frame(cur_stream);
                break;
            case SDLK_a:
                stream_cycle_channel(ffp, cur_stream, AVMEDIA_TYPE_AUDIO);
                break;
            case SDLK_v:
                stream_cycle_channel(ffp, cur_stream, AVMEDIA_TYPE_VIDEO);
                break;
            case SDLK_c:
                stream_cycle_channel(ffp, cur_stream, AVMEDIA_TYPE_VIDEO);
                stream_cycle_channel(ffp, cur_stream, AVMEDIA_TYPE_AUDIO);
                stream_cycle_channel(ffp, cur_stream, AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_t:
                stream_cycle_channel(ffp, cur_stream, AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_w:
#if CONFIG_AVFILTER
                if (cur_stream->show_mode == SHOW_MODE_VIDEO && cur_stream->vfilter_idx < ffp->nb_vfilters - 1) {
                    if (++cur_stream->vfilter_idx >= ffp->nb_vfilters)
                        cur_stream->vfilter_idx = 0;
                } else {
                    cur_stream->vfilter_idx = 0;
                    toggle_audio_display(cur_stream);
                }
#else
                toggle_audio_display(cur_stream);
#endif
                break;
            case SDLK_PAGEUP:
                if (cur_stream->ic->nb_chapters <= 1) {
                    incr = 600.0;
                    goto do_seek;
                }
                seek_chapter(ffp, 1);
                break;
            case SDLK_PAGEDOWN:
                if (cur_stream->ic->nb_chapters <= 1) {
                    incr = -600.0;
                    goto do_seek;
                }
                seek_chapter(ffp, -1);
                break;
            case SDLK_LEFT:
                incr = ffp->seek_interval ? -ffp->seek_interval : -10.0;
                goto do_seek;
            case SDLK_RIGHT:
                incr = ffp->seek_interval ? ffp->seek_interval : 10.0;
                goto do_seek;
            case SDLK_UP:
                incr = 60.0;
                goto do_seek;
            case SDLK_DOWN:
                incr = -60.0;
            do_seek:
                    if (ffp->seek_by_bytes) {
                        pos = -1;
                        if (pos < 0 && cur_stream->video_stream >= 0)
                            pos = frame_queue_last_pos(&cur_stream->pictq);
                        if (pos < 0 && cur_stream->audio_stream >= 0)
                            pos = frame_queue_last_pos(&cur_stream->sampq);
                        if (pos < 0)
                            pos = avio_tell(cur_stream->ic->pb);
                        if (cur_stream->ic->bit_rate)
                            incr *= cur_stream->ic->bit_rate / 8.0;
                        else
                            incr *= 180000.0;
                        pos += incr;
                        stream_seek(cur_stream, pos, incr, 1);
                    } else {
                        pos = get_master_clock(cur_stream);
                        if (isnan(pos))
                            pos = (double)cur_stream->seek_pos / AV_TIME_BASE;
                        pos += incr;
                        if (cur_stream->ic->start_time != AV_NOPTS_VALUE && pos < cur_stream->ic->start_time / (double)AV_TIME_BASE)
                            pos = cur_stream->ic->start_time / (double)AV_TIME_BASE;
                        stream_seek(cur_stream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
                    }
                break;
            default:
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ffp->exit_on_mousedown) {
                return; /* Exit event loop */
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                static int64_t last_mouse_left_click = 0;
                if (av_gettime_relative() - last_mouse_left_click <= 500000) {
                    toggle_full_screen(ffp, cur_stream);
                    cur_stream->force_refresh = 1;
                    last_mouse_left_click = 0;
                } else {
                    last_mouse_left_click = av_gettime_relative();
                }
            }
            /* fall through */
        case SDL_MOUSEMOTION:
            if (ffp->cursor_hidden) {
                SDL_ShowCursor(1);
                ffp->cursor_hidden = 0;
            }
            ffp->cursor_last_shown = av_gettime_relative();
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button != SDL_BUTTON_RIGHT)
                    break;
                x = event.button.x;
            } else {
                if (!(event.motion.state & SDL_BUTTON_RMASK))
                    break;
                x = event.motion.x;
            }
                if (ffp->seek_by_bytes || cur_stream->ic->duration <= 0) {
                    uint64_t size =  avio_size(cur_stream->ic->pb);
                    stream_seek(cur_stream, size*x/cur_stream->width, 0, 1);
                } else {
                    int64_t ts;
                    int ns, hh, mm, ss;
                    int tns, thh, tmm, tss;
                    tns  = cur_stream->ic->duration / 1000000LL;
                    thh  = tns / 3600;
                    tmm  = (tns % 3600) / 60;
                    tss  = (tns % 60);
                    frac = x / cur_stream->width;
                    ns   = frac * tns;
                    hh   = ns / 3600;
                    mm   = (ns % 3600) / 60;
                    ss   = (ns % 60);
                    av_log(NULL, AV_LOG_INFO,
                           "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
                            hh, mm, ss, thh, tmm, tss);
                    ts = frac * cur_stream->ic->duration;
                    if (cur_stream->ic->start_time != AV_NOPTS_VALUE)
                        ts += cur_stream->ic->start_time;
                    stream_seek(cur_stream, ts, 0, 0);
                }
            break;
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    ffp->screen_width  = cur_stream->width  = event.window.data1;
                    ffp->screen_height = cur_stream->height = event.window.data2;
                    /* 更新 OpenGL 视口大小 */
                    vout_set_size(ffp->vout, event.window.data1, event.window.data2);
                    if (cur_stream->vis_texture) {
                        vout_texture_destroy(cur_stream->vis_texture);
                        cur_stream->vis_texture = NULL;
                    }
                    /* fall through */
                case SDL_WINDOWEVENT_EXPOSED:
                    cur_stream->force_refresh = 1;
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            return; /* Exit event loop */
        default:
            break;
        }
    }
}
