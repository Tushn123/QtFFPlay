/*
 * Startup-to-first-frame timing profiler.
 */

#include "ff_startup_timer.h"

#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *stage_name(FFStartupStage stage)
{
    switch (stage) {
    case FF_ST_T0_PLAY_REQUEST:       return "T0_PLAY_REQUEST";
    case FF_ST_SDL_INIT_DONE:         return "SDL_INIT_DONE";
    case FF_ST_STREAM_OPEN_BEGIN:     return "STREAM_OPEN_BEGIN";
    case FF_ST_STREAM_OPEN_END:       return "STREAM_OPEN_END";
    case FF_ST_READ_THREAD_BEGIN:     return "READ_THREAD_BEGIN";
    case FF_ST_AVFORMAT_OPEN_BEGIN:   return "AVFORMAT_OPEN_BEGIN";
    case FF_ST_AVFORMAT_OPEN_END:     return "AVFORMAT_OPEN_END";
    case FF_ST_FIND_STREAM_INFO_BEGIN:return "FIND_STREAM_INFO_BEGIN";
    case FF_ST_FIND_STREAM_INFO_END:  return "FIND_STREAM_INFO_END";
    case FF_ST_OPEN_AUDIO:            return "OPEN_AUDIO";
    case FF_ST_OPEN_VIDEO:            return "OPEN_VIDEO";
    case FF_ST_FIRST_PACKET:          return "FIRST_PACKET";
    case FF_ST_FIRST_VIDEO_PACKET:    return "FIRST_VIDEO_PACKET";
    case FF_ST_VIDEO_DECODE_THREAD:   return "VIDEO_DECODE_THREAD";
    case FF_ST_FIRST_VIDEO_DECODED:   return "FIRST_VIDEO_DECODED";
    case FF_ST_FIRST_VIDEO_QUEUED:    return "FIRST_VIDEO_QUEUED";
    case FF_ST_FIRST_FRAME_TEXTURE:   return "FIRST_FRAME_TEXTURE";
    case FF_ST_FIRST_FRAME_PRESENT:   return "FIRST_FRAME_PRESENT";
    default:                          return "UNKNOWN";
    }
}

static struct {
    int enabled;
    int64_t origin_us;
    int64_t ts_us[FF_ST_COUNT];
    char detail[FF_ST_COUNT][64];
    int marked[FF_ST_COUNT];
    int reported;
} g_st;

static void report_startup_timing(void);

static int timer_enabled(void)
{
    const char *env = getenv("FF_STARTUP_TIMER");
    if (env && (env[0] == '0' || env[0] == 'f' || env[0] == 'F'))
        return 0;
    return 1;
}

void ff_startup_timer_reset(void)
{
    memset(&g_st, 0, sizeof(g_st));
    g_st.enabled = timer_enabled();
    g_st.origin_us = av_gettime_relative();
}

void ff_startup_timer_mark_detail(FFStartupStage stage, const char *detail)
{
    if (!g_st.enabled || stage < 0 || stage >= FF_ST_COUNT)
        return;
    if (g_st.marked[stage])
        return;

    g_st.marked[stage] = 1;
    g_st.ts_us[stage] = av_gettime_relative();
    if (detail && detail[0])
        av_strlcpy(g_st.detail[stage], detail, sizeof(g_st.detail[stage]));

    if (stage == FF_ST_FIRST_FRAME_PRESENT)
        report_startup_timing();
}

void ff_startup_timer_mark(FFStartupStage stage)
{
    ff_startup_timer_mark_detail(stage, NULL);
}

static double ms_since_origin(int64_t ts_us)
{
    if (ts_us <= 0)
        return -1.0;
    return (ts_us - g_st.origin_us) / 1000.0;
}

static double ms_between(int64_t t0, int64_t t1)
{
    if (t0 <= 0 || t1 <= 0 || t1 < t0)
        return -1.0;
    return (t1 - t0) / 1000.0;
}

static void print_stage_delta(const char *label, FFStartupStage from, FFStartupStage to)
{
    double delta = ms_between(g_st.ts_us[from], g_st.ts_us[to]);
    if (delta >= 0.0)
        av_log(NULL, AV_LOG_INFO, "[STARTUP-TIMER]   %-28s %8.2f ms\n", label, delta);
}

static void report_startup_timing(void)
{
    int i;
    double total_ms;

    if (!g_st.enabled || g_st.reported)
        return;
    if (!g_st.marked[FF_ST_FIRST_FRAME_PRESENT])
        return;

    g_st.reported = 1;
    total_ms = ms_since_origin(g_st.ts_us[FF_ST_FIRST_FRAME_PRESENT]);

    av_log(NULL, AV_LOG_INFO, "[STARTUP-TIMER] ========== Startup Timing Report ==========\n");
    for (i = 0; i < FF_ST_COUNT; i++) {
        if (!g_st.marked[i])
            continue;
        if (g_st.detail[i][0]) {
            av_log(NULL, AV_LOG_INFO,
                   "[STARTUP-TIMER]   %-28s %8.2f ms  (%s)\n",
                   stage_name((FFStartupStage)i),
                   ms_since_origin(g_st.ts_us[i]),
                   g_st.detail[i]);
        } else {
            av_log(NULL, AV_LOG_INFO,
                   "[STARTUP-TIMER]   %-28s %8.2f ms\n",
                   stage_name((FFStartupStage)i),
                   ms_since_origin(g_st.ts_us[i]));
        }
    }

    av_log(NULL, AV_LOG_INFO, "[STARTUP-TIMER] ---------- Stage Deltas ----------\n");
    print_stage_delta("SDL init", FF_ST_T0_PLAY_REQUEST, FF_ST_SDL_INIT_DONE);
    print_stage_delta("stream_open", FF_ST_SDL_INIT_DONE, FF_ST_STREAM_OPEN_END);
    print_stage_delta("read_thread start delay", FF_ST_STREAM_OPEN_END, FF_ST_READ_THREAD_BEGIN);
    print_stage_delta("avformat_open_input", FF_ST_AVFORMAT_OPEN_BEGIN, FF_ST_AVFORMAT_OPEN_END);
    print_stage_delta("find_stream_info", FF_ST_FIND_STREAM_INFO_BEGIN, FF_ST_FIND_STREAM_INFO_END);
    print_stage_delta("open A/V streams", FF_ST_FIND_STREAM_INFO_END, FF_ST_OPEN_VIDEO);
    print_stage_delta("first packet read", FF_ST_OPEN_VIDEO, FF_ST_FIRST_PACKET);
    print_stage_delta("first video packet queued", FF_ST_FIRST_PACKET, FF_ST_FIRST_VIDEO_PACKET);
    print_stage_delta("first frame decode", FF_ST_FIRST_VIDEO_PACKET, FF_ST_FIRST_VIDEO_DECODED);
    print_stage_delta("first frame queued", FF_ST_FIRST_VIDEO_DECODED, FF_ST_FIRST_VIDEO_QUEUED);
    print_stage_delta("first frame texture upload", FF_ST_FIRST_VIDEO_QUEUED, FF_ST_FIRST_FRAME_TEXTURE);
    print_stage_delta("first frame present", FF_ST_FIRST_FRAME_TEXTURE, FF_ST_FIRST_FRAME_PRESENT);
    av_log(NULL, AV_LOG_INFO,
           "[STARTUP-TIMER] >>> Total startup (T0 -> first frame present): %.2f ms <<<\n", total_ms);
    av_log(NULL, AV_LOG_INFO, "[STARTUP-TIMER] ====================================\n");
    fflush(stderr);
}
