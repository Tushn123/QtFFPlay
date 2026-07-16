/*
 * Startup-to-first-frame timing profiler (decoupled from playback logic).
 *
 * Usage:
 *   ff_startup_timer_reset();          // once before playback starts
 *   ff_startup_timer_mark(FF_ST_...);  // mark stages (first hit only)
 *
 * Set FF_STARTUP_TIMER=0 to disable output.
 */

#ifndef FF_STARTUP_TIMER_H
#define FF_STARTUP_TIMER_H

typedef enum {
    FF_ST_T0_PLAY_REQUEST = 0,   /* play request (before stream_open) */
    FF_ST_SDL_INIT_DONE,          /* SDL/window/renderer ready */
    FF_ST_STREAM_OPEN_BEGIN,
    FF_ST_STREAM_OPEN_END,       /* read_thread created */
    FF_ST_READ_THREAD_BEGIN,
    FF_ST_AVFORMAT_OPEN_BEGIN,
    FF_ST_AVFORMAT_OPEN_END,
    FF_ST_FIND_STREAM_INFO_BEGIN,
    FF_ST_FIND_STREAM_INFO_END,
    FF_ST_OPEN_AUDIO,            /* after stream_component_open(audio) */
    FF_ST_OPEN_VIDEO,            /* after stream_component_open(video) */
    FF_ST_FIRST_PACKET,          /* first successful av_read_frame */
    FF_ST_FIRST_VIDEO_PACKET,    /* first video packet enqueued */
    FF_ST_VIDEO_DECODE_THREAD,   /* video_thread started */
    FF_ST_FIRST_VIDEO_DECODED,   /* first video frame decoded */
    FF_ST_FIRST_VIDEO_QUEUED,    /* first video frame in pictq */
    FF_ST_FIRST_FRAME_TEXTURE,   /* first frame texture uploaded */
    FF_ST_FIRST_FRAME_PRESENT,   /* first RenderPresent (startup end) */
    FF_ST_COUNT
} FFStartupStage;

void ff_startup_timer_reset(void);
void ff_startup_timer_mark(FFStartupStage stage);
void ff_startup_timer_mark_detail(FFStartupStage stage, const char *detail);

#endif /* FF_STARTUP_TIMER_H */
