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

/* extern global variables */
extern const char program_name[];
extern const int program_birth_year;

extern unsigned sws_flags;

extern const AVInputFormat *file_iformat;
extern const char *input_filename;
extern const char *window_title;
extern int default_width;
extern int default_height;
extern int screen_width;
extern int screen_height;
extern int screen_left;
extern int screen_top;
extern int audio_disable;
extern int video_disable;
extern int subtitle_disable;
extern const char* wanted_stream_spec[AVMEDIA_TYPE_NB];
extern int seek_by_bytes;
extern float seek_interval;
extern int display_disable;
extern int borderless;
extern int alwaysontop;
extern int startup_volume;
extern int show_status;
extern int av_sync_type;
extern int64_t start_time;
extern int64_t duration;
extern int fast;
extern int genpts;
extern int lowres;
extern int decoder_reorder_pts;
extern int autoexit;
extern int exit_on_keydown;
extern int exit_on_mousedown;
extern int loop;
extern int framedrop;
extern int infinite_buffer;
extern enum ShowMode show_mode;
extern const char *audio_codec_name;
extern const char *subtitle_codec_name;
extern const char *video_codec_name;
extern double rdftspeed;
extern int64_t cursor_last_shown;
extern int cursor_hidden;
#if CONFIG_AVFILTER
extern const char **vfilters_list;
extern int nb_vfilters;
extern char *afilters;
#endif
extern int autorotate;
extern int find_stream_info;
extern int filter_nbthreads;

extern int is_full_screen;
extern int64_t audio_callback_time;

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_RendererInfo renderer_info;
extern SDL_AudioDeviceID audio_dev;


/* Video rendering functions */
void fill_rectangle(int x, int y, int w, int h);
int realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);
void calculate_display_rect(SDL_Rect *rect, int scr_xleft, int scr_ytop, int scr_width, int scr_height, int pic_width, int pic_height, AVRational pic_sar);
void get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode);
int upload_texture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx);
void set_sdl_yuv_conversion_mode(AVFrame *frame);
void video_image_display(VideoState *is);
void video_audio_display(VideoState *s);
void set_default_window_size(int width, int height, AVRational sar);
int video_open(VideoState *is);
void video_display(VideoState *is);
void video_refresh(void *opaque, double *remaining_time);
int queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);
int get_video_frame(VideoState *is, AVFrame *frame);

/* Audio output functions */
int compute_mod(int a, int b);
void update_sample_display(VideoState *is, short *samples, int samples_size);
int synchronize_audio(VideoState *is, int nb_samples);
int audio_decode_frame(VideoState *is);
void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
int audio_open(void *opaque, AVChannelLayout *wanted_channel_layout, int wanted_sample_rate, struct AudioParams *audio_hw_params);

/* Filter functions */
#if CONFIG_AVFILTER
int opt_add_vfilter(void *optctx, const char *opt, const char *arg);
int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph, AVFilterContext *source_ctx, AVFilterContext *sink_ctx);
int configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame);
int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format);
#endif

/* Decode thread functions */
int audio_thread(void *arg);
int video_thread(void *arg);
int subtitle_thread(void *arg);

/* Stream management functions */
void stream_component_close(VideoState *is, int stream_index);
void stream_close(VideoState *is);
int stream_component_open(VideoState *is, int stream_index);
VideoState *stream_open(const char *filename, const AVInputFormat *iformat);
void stream_seek(VideoState *is, int64_t pos, int64_t rel, int by_bytes);
void stream_toggle_pause(VideoState *is);
void toggle_pause(VideoState *is);
void toggle_mute(VideoState *is);
void update_volume(VideoState *is, int sign, double step);
void step_to_next_frame(VideoState *is);
void stream_cycle_channel(VideoState *is, int codec_type);
void toggle_full_screen(VideoState *is);
void toggle_audio_display(VideoState *is);

/* Read thread functions */
int decode_interrupt_cb(void *ctx);
int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);
int is_realtime(AVFormatContext *s);
int read_thread(void *arg);

#endif /* FFPLAY_H */

