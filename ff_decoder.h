/**
 * @file
 * ffplay decoder declarations
 */

#ifndef FF_DECODER_H
#define FF_DECODER_H

#include "ff_types.h"

/* Decoder functions */
int decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond);
int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub, int decoder_reorder_pts);
void decoder_destroy(Decoder *d);
void decoder_abort(Decoder *d, FrameQueue *fq);
int decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void* arg);

#endif /* FF_DECODER_H */

