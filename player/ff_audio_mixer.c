/*
 * ff_audio_mixer.c - SDL2 音频混音器实现
 * 
 * 实现原理：
 * 1. 创建单一 SDL 音频设备，注册混音器回调
 * 2. 每个播放器创建独立的 AudioStream，有自己的环形缓冲区
 * 3. 混音器回调从所有活跃流读取数据，混合后输出
 * 4. 每个流可以独立控制音量、暂停、停止
 */

#include "ff_audio_mixer.h"
#include <string.h>
#include <libavutil/mem.h>
#include <libavutil/log.h>

/* 全局混音器单例 */
static AudioMixer *g_mixer = NULL;
static SDL_mutex *g_mixer_mutex = NULL;

/*
 * =============================================================================
 * 环形缓冲区操作
 * =============================================================================
 */

/* 计算缓冲区中的数据量 */
static inline int ring_buffer_queued(AudioStream *stream)
{
    int write_pos = stream->write_pos;
    int read_pos = stream->read_pos;
    return (write_pos - read_pos + stream->ring_size) % stream->ring_size;
}

/* 计算缓冲区可写空间 */
static inline int ring_buffer_free(AudioStream *stream)
{
    return stream->ring_size - ring_buffer_queued(stream) - 1;
}

/* 写入数据到环形缓冲区 */
static int ring_buffer_write(AudioStream *stream, const uint8_t *data, int len)
{
    SDL_LockMutex(stream->buffer_mutex);
    
    int free_space = ring_buffer_free(stream);
    if (len > free_space) {
        len = free_space;  /* 只写入可用空间 */
    }
    
    if (len > 0) {
        int write_pos = stream->write_pos;
        int first_part = stream->ring_size - write_pos;
        
        if (first_part >= len) {
            memcpy(stream->ring_buffer + write_pos, data, len);
        } else {
            memcpy(stream->ring_buffer + write_pos, data, first_part);
            memcpy(stream->ring_buffer, data + first_part, len - first_part);
        }
        stream->write_pos = (write_pos + len) % stream->ring_size;
    }
    
    SDL_UnlockMutex(stream->buffer_mutex);
    return len;
}

/* 从环形缓冲区读取数据 */
static int ring_buffer_read(AudioStream *stream, uint8_t *data, int len)
{
    SDL_LockMutex(stream->buffer_mutex);
    
    int queued = ring_buffer_queued(stream);
    if (len > queued) {
        len = queued;
    }
    
    if (len > 0) {
        int read_pos = stream->read_pos;
        int first_part = stream->ring_size - read_pos;
        
        if (first_part >= len) {
            memcpy(data, stream->ring_buffer + read_pos, len);
        } else {
            memcpy(data, stream->ring_buffer + read_pos, first_part);
            memcpy(data + first_part, stream->ring_buffer, len - first_part);
        }
        stream->read_pos = (read_pos + len) % stream->ring_size;
        
        /* 通知写入线程有空间了 */
        if (stream->buffer_cond) {
            SDL_CondSignal(stream->buffer_cond);
        }
    }
    
    SDL_UnlockMutex(stream->buffer_mutex);
    return len;
}

/* 清空环形缓冲区 */
static void ring_buffer_flush(AudioStream *stream)
{
    SDL_LockMutex(stream->buffer_mutex);
    stream->read_pos = 0;
    stream->write_pos = 0;
    if (stream->buffer_cond) {
        SDL_CondSignal(stream->buffer_cond);
    }
    SDL_UnlockMutex(stream->buffer_mutex);
}

/*
 * =============================================================================
 * SDL 音频回调 - 核心混音逻辑
 * =============================================================================
 */

static void mixer_audio_callback(void *opaque, Uint8 *stream, int len)
{
    AudioMixer *mixer = (AudioMixer *)opaque;
    
    /* 首先清零输出 */
    memset(stream, 0, len);
    
    if (!mixer || !mixer->running) {
        return;
    }
    
    /* 计算采样数（假设 16-bit stereo）*/
    int samples = len / sizeof(int16_t);
    
    /* 确保混音缓冲区足够大 */
    if (samples > mixer->mix_buffer_samples) {
        return;
    }
    
    /* 清零混音缓冲区 */
    memset(mixer->mix_buffer, 0, samples * sizeof(int32_t));
    
    SDL_LockMutex(mixer->streams_mutex);
    
    int active_count = 0;
    
    /* 遍历所有活跃流，读取并累加音频数据 */
    for (int i = 0; i < MIXER_MAX_STREAMS; i++) {
        AudioStream *as = mixer->streams[i];
        if (!as || !as->active || as->state != STREAM_STATE_PLAYING) {
            continue;
        }
        
        /* 从流的环形缓冲区读取数据 */
        int read_len = ring_buffer_read(as, mixer->temp_buffer, len);
        
        if (read_len > 0) {
            active_count++;
            int16_t *src = (int16_t *)mixer->temp_buffer;
            int src_samples = read_len / sizeof(int16_t);
            
            /* 累加到混音缓冲区（带音量调节）
             * 使用 32-bit 累加避免溢出 */
            int vol = as->volume;
            for (int j = 0; j < src_samples && j < samples; j++) {
                mixer->mix_buffer[j] += ((int32_t)src[j] * vol) >> 7;  /* /128 */
            }
        }
    }
    
    SDL_UnlockMutex(mixer->streams_mutex);
    
    /* 将混音结果写入输出（带削波保护）*/
    int16_t *out = (int16_t *)stream;
    for (int i = 0; i < samples; i++) {
        int32_t sample = mixer->mix_buffer[i];
        /* 软削波 */
        if (sample > 32767) sample = 32767;
        else if (sample < -32768) sample = -32768;
        out[i] = (int16_t)sample;
    }
}

/*
 * =============================================================================
 * 混音器生命周期管理
 * =============================================================================
 */

AudioMixer *audio_mixer_create(int sample_rate, int channels, int samples)
{
    /* 初始化全局互斥锁 */
    if (!g_mixer_mutex) {
        g_mixer_mutex = SDL_CreateMutex();
    }
    
    SDL_LockMutex(g_mixer_mutex);
    
    /* 如果已存在，直接返回 */
    if (g_mixer) {
        av_log(NULL, AV_LOG_INFO, "[MIXER] Returning existing mixer (streams=%d)\n",
               g_mixer->stream_count);
        SDL_UnlockMutex(g_mixer_mutex);
        return g_mixer;
    }
    
    AudioMixer *mixer = (AudioMixer *)av_mallocz(sizeof(AudioMixer));
    if (!mixer) {
        SDL_UnlockMutex(g_mixer_mutex);
        return NULL;
    }
    
    /* 创建互斥锁 */
    mixer->streams_mutex = SDL_CreateMutex();
    if (!mixer->streams_mutex) {
        av_free(mixer);
        SDL_UnlockMutex(g_mixer_mutex);
        return NULL;
    }
    
    /* 分配混音缓冲区 */
    mixer->mix_buffer_samples = samples * channels * 2;  /* 预留空间 */
    mixer->mix_buffer = (int32_t *)av_malloc(mixer->mix_buffer_samples * sizeof(int32_t));
    
    /* 分配临时读取缓冲区 */
    mixer->temp_buffer_size = samples * channels * sizeof(int16_t) * 2;
    mixer->temp_buffer = (uint8_t *)av_malloc(mixer->temp_buffer_size);
    
    if (!mixer->mix_buffer || !mixer->temp_buffer) {
        av_log(NULL, AV_LOG_ERROR, "[MIXER] Failed to allocate buffers\n");
        goto fail;
    }
    
    /* 配置 SDL 音频 */
    SDL_AudioSpec wanted_spec = {0};
    wanted_spec.freq = sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = channels;
    wanted_spec.samples = samples;
    wanted_spec.callback = mixer_audio_callback;
    wanted_spec.userdata = mixer;
    
    mixer->audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, 
                                            &mixer->audio_spec,
                                            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                            SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    
    if (mixer->audio_dev == 0) {
        av_log(NULL, AV_LOG_ERROR, "[MIXER] Failed to open audio device: %s\n", 
               SDL_GetError());
        goto fail;
    }
    
    av_log(NULL, AV_LOG_INFO, 
           "[MIXER] Created: dev=%u, rate=%d, channels=%d, samples=%d\n",
           mixer->audio_dev, mixer->audio_spec.freq, 
           mixer->audio_spec.channels, mixer->audio_spec.samples);
    
    mixer->running = 1;
    SDL_PauseAudioDevice(mixer->audio_dev, 0);  /* 开始播放 */
    
    g_mixer = mixer;
    SDL_UnlockMutex(g_mixer_mutex);
    return mixer;
    
fail:
    if (mixer->streams_mutex) SDL_DestroyMutex(mixer->streams_mutex);
    if (mixer->mix_buffer) av_free(mixer->mix_buffer);
    if (mixer->temp_buffer) av_free(mixer->temp_buffer);
    av_free(mixer);
    SDL_UnlockMutex(g_mixer_mutex);
    return NULL;
}

AudioMixer *audio_mixer_get_global(void)
{
    return g_mixer;
}

void audio_mixer_destroy(AudioMixer *mixer)
{
    if (!mixer) return;
    
    if (g_mixer_mutex) {
        SDL_LockMutex(g_mixer_mutex);
    }
    
    mixer->running = 0;
    
    /* 停止并关闭 SDL 音频设备 */
    if (mixer->audio_dev) {
        SDL_PauseAudioDevice(mixer->audio_dev, 1);
        SDL_Delay(50);  /* 等待回调结束 */
        SDL_CloseAudioDevice(mixer->audio_dev);
        mixer->audio_dev = 0;
    }
    
    /* 清理所有流 */
    SDL_LockMutex(mixer->streams_mutex);
    for (int i = 0; i < MIXER_MAX_STREAMS; i++) {
        if (mixer->streams[i]) {
            AudioStream *stream = mixer->streams[i];
            if (stream->buffer_mutex) {
                SDL_DestroyMutex(stream->buffer_mutex);
            }
            if (stream->buffer_cond) {
                SDL_DestroyCond(stream->buffer_cond);
            }
            if (stream->ring_buffer) {
                av_free(stream->ring_buffer);
            }
            av_free(stream);
            mixer->streams[i] = NULL;
        }
    }
    SDL_UnlockMutex(mixer->streams_mutex);
    
    SDL_DestroyMutex(mixer->streams_mutex);
    av_free(mixer->mix_buffer);
    av_free(mixer->temp_buffer);
    
    if (g_mixer == mixer) {
        g_mixer = NULL;
    }
    
    av_free(mixer);
    av_log(NULL, AV_LOG_INFO, "[MIXER] Destroyed\n");
    
    if (g_mixer_mutex) {
        SDL_UnlockMutex(g_mixer_mutex);
    }
}

int audio_mixer_get_sample_rate(AudioMixer *mixer)
{
    return mixer ? mixer->audio_spec.freq : 0;
}

int audio_mixer_get_channels(AudioMixer *mixer)
{
    return mixer ? mixer->audio_spec.channels : 0;
}

/*
 * =============================================================================
 * 音频流管理
 * =============================================================================
 */

AudioStream *audio_mixer_add_stream(AudioMixer *mixer, void *userdata)
{
    if (!mixer) {
        av_log(NULL, AV_LOG_ERROR, "[MIXER] Cannot add stream: mixer is NULL\n");
        return NULL;
    }
    
    AudioStream *stream = (AudioStream *)av_mallocz(sizeof(AudioStream));
    if (!stream) {
        return NULL;
    }
    
    /* 分配环形缓冲区 */
    stream->ring_buffer = (uint8_t *)av_malloc(MIXER_RING_BUF_SIZE);
    stream->buffer_mutex = SDL_CreateMutex();
    stream->buffer_cond = SDL_CreateCond();
    
    if (!stream->ring_buffer || !stream->buffer_mutex || !stream->buffer_cond) {
        av_log(NULL, AV_LOG_ERROR, "[MIXER] Failed to allocate stream resources\n");
        if (stream->ring_buffer) av_free(stream->ring_buffer);
        if (stream->buffer_mutex) SDL_DestroyMutex(stream->buffer_mutex);
        if (stream->buffer_cond) SDL_DestroyCond(stream->buffer_cond);
        av_free(stream);
        return NULL;
    }
    
    stream->ring_size = MIXER_RING_BUF_SIZE;
    stream->read_pos = 0;
    stream->write_pos = 0;
    stream->volume = 128;  /* 默认最大音量 */
    stream->state = STREAM_STATE_IDLE;
    stream->active = 0;
    stream->userdata = userdata;
    
    /* 找到空闲槽位并添加 */
    SDL_LockMutex(mixer->streams_mutex);
    
    int slot = -1;
    for (int i = 0; i < MIXER_MAX_STREAMS; i++) {
        if (mixer->streams[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot >= 0) {
        stream->id = slot;
        mixer->streams[slot] = stream;
        mixer->stream_count++;
        av_log(NULL, AV_LOG_INFO, 
               "[MIXER] Added stream %d (userdata=%p, total=%d)\n",
               slot, userdata, mixer->stream_count);
    } else {
        av_log(NULL, AV_LOG_ERROR, "[MIXER] No free stream slots!\n");
    }
    
    SDL_UnlockMutex(mixer->streams_mutex);
    
    if (slot < 0) {
        av_free(stream->ring_buffer);
        SDL_DestroyMutex(stream->buffer_mutex);
        SDL_DestroyCond(stream->buffer_cond);
        av_free(stream);
        return NULL;
    }
    
    return stream;
}

void audio_mixer_remove_stream(AudioMixer *mixer, AudioStream *stream)
{
    if (!mixer || !stream) return;
    
    /* 先停止流 */
    stream->active = 0;
    stream->state = STREAM_STATE_IDLE;
    
    SDL_LockMutex(mixer->streams_mutex);
    
    for (int i = 0; i < MIXER_MAX_STREAMS; i++) {
        if (mixer->streams[i] == stream) {
            mixer->streams[i] = NULL;
            mixer->stream_count--;
            av_log(NULL, AV_LOG_INFO, 
                   "[MIXER] Removed stream %d (remaining=%d)\n",
                   i, mixer->stream_count);
            break;
        }
    }
    
    SDL_UnlockMutex(mixer->streams_mutex);
    
    /* 等待一下确保回调不再访问此流 */
    SDL_Delay(20);
    
    /* 清理流资源 */
    if (stream->buffer_cond) {
        SDL_DestroyCond(stream->buffer_cond);
    }
    if (stream->buffer_mutex) {
        SDL_DestroyMutex(stream->buffer_mutex);
    }
    if (stream->ring_buffer) {
        av_free(stream->ring_buffer);
    }
    av_free(stream);
}

/*
 * =============================================================================
 * 音频流控制
 * =============================================================================
 */

void audio_stream_play(AudioStream *stream)
{
    if (stream) {
        stream->state = STREAM_STATE_PLAYING;
        stream->active = 1;
        av_log(NULL, AV_LOG_INFO, "[MIXER] Stream %d: PLAYING\n", stream->id);
    }
}

void audio_stream_pause(AudioStream *stream)
{
    if (stream) {
        stream->state = STREAM_STATE_PAUSED;
        av_log(NULL, AV_LOG_DEBUG, "[MIXER] Stream %d: PAUSED\n", stream->id);
    }
}

void audio_stream_stop(AudioStream *stream)
{
    if (stream) {
        stream->state = STREAM_STATE_IDLE;
        stream->active = 0;
        ring_buffer_flush(stream);
        av_log(NULL, AV_LOG_INFO, "[MIXER] Stream %d: STOPPED\n", stream->id);
    }
}

void audio_stream_set_volume(AudioStream *stream, int volume)
{
    if (stream) {
        if (volume < 0) volume = 0;
        if (volume > 128) volume = 128;
        stream->volume = volume;
    }
}

/*
 * =============================================================================
 * 音频数据操作
 * =============================================================================
 */

int audio_stream_write(AudioStream *stream, const uint8_t *data, int len)
{
    if (!stream || !data || len <= 0) {
        return 0;
    }
    
    if (stream->state == STREAM_STATE_IDLE) {
        return 0;  /* 流未激活，丢弃数据 */
    }
    
    return ring_buffer_write(stream, data, len);
}

int audio_stream_get_queued(AudioStream *stream)
{
    if (!stream) return 0;
    
    SDL_LockMutex(stream->buffer_mutex);
    int queued = ring_buffer_queued(stream);
    SDL_UnlockMutex(stream->buffer_mutex);
    
    return queued;
}

int audio_stream_get_free_space(AudioStream *stream)
{
    if (!stream) return 0;
    
    SDL_LockMutex(stream->buffer_mutex);
    int free_space = ring_buffer_free(stream);
    SDL_UnlockMutex(stream->buffer_mutex);
    
    return free_space;
}

void audio_stream_flush(AudioStream *stream)
{
    if (stream) {
        ring_buffer_flush(stream);
    }
}

