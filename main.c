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
 * simple media player based on the FFmpeg libraries
 */

#include "ffplay.h"

/* Global variables */
const char program_name[] = "ffplay";
const int program_birth_year = 2025;

static void sigterm_handler(int sig)
{
    exit(123);
}

/* Called from the main */
int main(int argc, char **argv)
{
    FFPlayer *ffp = NULL;
    const char *input_filename = "C:/shn/media/animal.mp4";

    /* Global initialization */
    ffp_global_init();

    signal(SIGINT , sigterm_handler);
    signal(SIGTERM, sigterm_handler);

    /* Create FFPlayer instance */
    ffp = ffp_create();
    if (!ffp) {
        av_log(NULL, AV_LOG_FATAL, "Failed to create FFPlayer!\n");
        return 1;
    }

    /* Initialize SDL */
    if (ffp_init_sdl(ffp) < 0) {
        ffp_destroy(ffp);
        return 1;
    }

    /* Create window and renderer */
    if (ffp_create_window(ffp) < 0) {
        ffp_shutdown(ffp);
        return 1;
    }

    /* Prepare and open the stream */
    if (ffp_prepare_async(ffp, input_filename) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
        ffp_shutdown(ffp);
        return 1;
    }

    /* Run the event loop */
    ffp_event_loop(ffp);

    /* Clean up */
    ffp_shutdown(ffp);

    /* Global cleanup */
    ffp_global_uninit();

    return 0;
}
