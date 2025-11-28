/**
 * @file
 * ffplay stream management and audio/video output declarations
 */

#ifndef FFPLAY_H
#define FFPLAY_H

#include "ff_types.h"
#include "ff_queue.h"
#include "ff_decoder.h"
#include "ff_clock.h"
#include "ff_ffplayer.h"

/* extern global variables - kept for command line option parsing compatibility */
extern const char program_name[];
extern const int program_birth_year;

/* Video rendering functions */
void fill_rectangle(FFPlayer *ffp, int x, int y, int w, int h);
int realloc_texture(FFPlayer *ffp, SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);
void calculate_display_rect(SDL_Rect *rect, int scr_xleft, int scr_ytop, int scr_width, int scr_height, int pic_width, int pic_height, AVRational pic_sar);
void get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode);
int upload_texture(FFPlayer *ffp, SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx);
void set_sdl_yuv_conversion_mode(AVFrame *frame);
void video_image_display(FFPlayer *ffp, VideoState *is);
void video_audio_display(FFPlayer *ffp, VideoState *s);
void set_default_window_size(FFPlayer *ffp, int width, int height, AVRational sar);
int video_open(FFPlayer *ffp, VideoState *is);
void video_display(FFPlayer *ffp, VideoState *is);
void video_refresh(FFPlayer *ffp, VideoState *is, double *remaining_time);
int queue_picture(FFPlayer *ffp, VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);
int get_video_frame(FFPlayer *ffp, VideoState *is, AVFrame *frame);

/* Audio output functions */
int compute_mod(int a, int b);
void update_sample_display(VideoState *is, short *samples, int samples_size);
int synchronize_audio(FFPlayer *ffp, VideoState *is, int nb_samples);
int audio_decode_frame(FFPlayer *ffp, VideoState *is);
void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
int audio_open(FFPlayer *ffp, void *opaque, AVChannelLayout *wanted_channel_layout, int wanted_sample_rate, struct AudioParams *audio_hw_params);

/* Filter functions */
#if CONFIG_AVFILTER
int opt_add_vfilter(FFPlayer *ffp, void *optctx, const char *opt, const char *arg);
int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph, AVFilterContext *source_ctx, AVFilterContext *sink_ctx);
int configure_video_filters(FFPlayer *ffp, AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame);
int configure_audio_filters(FFPlayer *ffp, VideoState *is, const char *afilters, int force_output_format);
#endif

/* Decode thread functions */
int audio_thread(void *arg);
int video_thread(void *arg);
int subtitle_thread(void *arg);

/* Stream management functions */
void stream_component_close(FFPlayer *ffp, VideoState *is, int stream_index);
void stream_close(FFPlayer *ffp, VideoState *is);
int stream_component_open(FFPlayer *ffp, VideoState *is, int stream_index);
VideoState *stream_open(FFPlayer *ffp, const char *filename, const AVInputFormat *iformat);
void stream_seek(VideoState *is, int64_t pos, int64_t rel, int by_bytes);
void stream_toggle_pause(VideoState *is);
void toggle_pause(VideoState *is);
void toggle_mute(VideoState *is);
void update_volume(VideoState *is, int sign, double step);
void step_to_next_frame(VideoState *is);
void stream_cycle_channel(FFPlayer *ffp, VideoState *is, int codec_type);
void toggle_full_screen(FFPlayer *ffp, VideoState *is);
void toggle_audio_display(VideoState *is);

/* Read thread functions */
int decode_interrupt_cb(void *ctx);
int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);
int is_realtime(AVFormatContext *s);
int read_thread(void *arg);

#endif /* FFPLAY_H */
