/*
 * ff_ffmsg.h
 *      Message type definitions for MediaPlayer
 *      Based on ijkplayer's ff_ffmsg.h
 *
 * Copyright (c) 2013 Bilibili
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef FF_FFMSG_H
#define FF_FFMSG_H

/*
 * =============================================================================
 * 系统消息 (0-99)
 * =============================================================================
 */
#define FFP_MSG_FLUSH                       0

/*
 * =============================================================================
 * 错误消息 (100-199)
 * =============================================================================
 */
#define FFP_MSG_ERROR                       100     /* arg1 = error code */

/*
 * =============================================================================
 * 状态消息 (200-399)
 * =============================================================================
 */
#define FFP_MSG_PREPARED                    200
#define FFP_MSG_COMPLETED                   300

/*
 * =============================================================================
 * 视频相关消息 (400-499)
 * =============================================================================
 */
#define FFP_MSG_VIDEO_SIZE_CHANGED          400     /* arg1 = width, arg2 = height */
#define FFP_MSG_SAR_CHANGED                 401     /* arg1 = sar.num, arg2 = sar.den */
#define FFP_MSG_VIDEO_RENDERING_START       402
#define FFP_MSG_AUDIO_RENDERING_START       403
#define FFP_MSG_VIDEO_ROTATION_CHANGED      404     /* arg1 = degree */
#define FFP_MSG_AUDIO_DECODED_START         405
#define FFP_MSG_VIDEO_DECODED_START         406
#define FFP_MSG_OPEN_INPUT                  407
#define FFP_MSG_FIND_STREAM_INFO            408
#define FFP_MSG_COMPONENT_OPEN              409
#define FFP_MSG_VIDEO_SEEK_RENDERING_START  410
#define FFP_MSG_AUDIO_SEEK_RENDERING_START  411

/*
 * =============================================================================
 * 缓冲消息 (500-599)
 * =============================================================================
 */
#define FFP_MSG_BUFFERING_START             500
#define FFP_MSG_BUFFERING_END               501
#define FFP_MSG_BUFFERING_UPDATE            502     /* arg1 = buffering head position in time, arg2 = minimum percent */
#define FFP_MSG_BUFFERING_BYTES_UPDATE      503     /* arg1 = cached data in bytes, arg2 = high water mark */
#define FFP_MSG_BUFFERING_TIME_UPDATE       504     /* arg1 = cached duration in ms, arg2 = high water mark */

/*
 * =============================================================================
 * Seek消息 (600-699)
 * =============================================================================
 */
#define FFP_MSG_SEEK_COMPLETE               600     /* arg1 = seek position, arg2 = error */

/*
 * =============================================================================
 * 状态变化消息 (700-799)
 * =============================================================================
 */
#define FFP_MSG_PLAYBACK_STATE_CHANGED      700
#define FFP_MSG_MEDIA_TYPE_CHANGED          701     /* arg1 = media_type (FFPMediaType), arg2 = is_seekable */

/*
 * =============================================================================
 * 字幕消息 (800-899)
 * =============================================================================
 */
#define FFP_MSG_TIMED_TEXT                  800

/*
 * =============================================================================
 * 请求消息 (20000+)
 * 这些是内部请求，用于控制播放器行为
 * =============================================================================
 */
#define FFP_REQ_START                       20001
#define FFP_REQ_PAUSE                       20002
#define FFP_REQ_SEEK                        20003

/*
 * =============================================================================
 * 属性ID - 浮点类型
 * =============================================================================
 */
#define FFP_PROP_FLOAT_VIDEO_DECODE_FRAMES_PER_SECOND   10001
#define FFP_PROP_FLOAT_VIDEO_OUTPUT_FRAMES_PER_SECOND   10002
#define FFP_PROP_FLOAT_PLAYBACK_RATE                    10003
#define FFP_PROP_FLOAT_AVDELAY                          10004
#define FFP_PROP_FLOAT_AVDIFF                           10005
#define FFP_PROP_FLOAT_PLAYBACK_VOLUME                  10006
#define FFP_PROP_FLOAT_DROP_FRAME_RATE                  10007

/*
 * =============================================================================
 * 属性ID - 整数类型
 * =============================================================================
 */
#define FFP_PROP_INT64_CURRENT_POSITION                 20000
#define FFP_PROP_INT64_DURATION                         20001
#define FFP_PROP_INT64_SELECTED_VIDEO_STREAM            20002
#define FFP_PROP_INT64_SELECTED_AUDIO_STREAM            20003
#define FFP_PROP_INT64_VIDEO_DECODER                    20004
#define FFP_PROP_INT64_AUDIO_DECODER                    20005
#define FFP_PROP_INT64_VIDEO_CACHED_DURATION            20006
#define FFP_PROP_INT64_AUDIO_CACHED_DURATION            20007
#define FFP_PROP_INT64_VIDEO_CACHED_BYTES               20008
#define FFP_PROP_INT64_AUDIO_CACHED_BYTES               20009
#define FFP_PROP_INT64_VIDEO_CACHED_PACKETS             20010
#define FFP_PROP_INT64_AUDIO_CACHED_PACKETS             20011
#define FFP_PROP_INT64_BIT_RATE                         20100

#endif /* FF_FFMSG_H */

