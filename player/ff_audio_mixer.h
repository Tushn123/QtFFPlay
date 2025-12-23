/*
 * ff_audio_mixer.h - SDL2 音频混音器
 * 支持多个播放器共享单一 SDL 音频设备
 * 
 * 架构：
 *   Player 1 ──→ AudioStream 1 ──┐
 *   Player 2 ──→ AudioStream 2 ──┼──→ AudioMixer ──→ SDL Audio Device
 *   Player 3 ──→ AudioStream 3 ──┘
 */

#ifndef FF_AUDIO_MIXER_H
#define FF_AUDIO_MIXER_H

#include <SDL.h>
#include <stdint.h>

#define MIXER_MAX_STREAMS    16
#define MIXER_RING_BUF_SIZE  (64 * 1024)  /* 64KB 环形缓冲区 */

/* 音频流状态 */
typedef enum {
    STREAM_STATE_IDLE = 0,
    STREAM_STATE_PLAYING,
    STREAM_STATE_PAUSED,
} AudioStreamState;

/* 单个音频流（对应一个播放器）*/
typedef struct AudioStream {
    int id;                          /* 流 ID */
    volatile AudioStreamState state; /* 流状态 */
    volatile int active;             /* 是否激活（用于快速检查）*/
    
    /* 环形缓冲区 */
    uint8_t *ring_buffer;
    int ring_size;
    volatile int read_pos;
    volatile int write_pos;
    SDL_mutex *buffer_mutex;
    SDL_cond *buffer_cond;           /* 缓冲区有空间时通知 */
    
    /* 音量控制 (0-128, 128=最大) */
    int volume;
    
    /* 用户数据 */
    void *userdata;
    
} AudioStream;

/* 混音器 */
typedef struct AudioMixer {
    SDL_AudioDeviceID audio_dev;
    SDL_AudioSpec audio_spec;
    
    AudioStream *streams[MIXER_MAX_STREAMS];
    int stream_count;
    SDL_mutex *streams_mutex;
    
    /* 混音缓冲区（32位累加，避免溢出）*/
    int32_t *mix_buffer;
    int mix_buffer_samples;
    
    /* 临时读取缓冲区 */
    uint8_t *temp_buffer;
    int temp_buffer_size;
    
    volatile int running;
    
} AudioMixer;

/*
 * =============================================================================
 * 混音器 API
 * =============================================================================
 */

/* 创建/获取全局混音器（单例模式）
 * @param sample_rate  采样率（如 44100）
 * @param channels     声道数（如 2）
 * @param samples      每次回调的采样数（如 1024）
 * @return 混音器指针，失败返回 NULL
 */
AudioMixer *audio_mixer_create(int sample_rate, int channels, int samples);

/* 获取全局混音器（不创建）*/
AudioMixer *audio_mixer_get_global(void);

/* 销毁混音器 */
void audio_mixer_destroy(AudioMixer *mixer);

/* 获取混音器音频参数 */
int audio_mixer_get_sample_rate(AudioMixer *mixer);
int audio_mixer_get_channels(AudioMixer *mixer);

/*
 * =============================================================================
 * 音频流 API
 * =============================================================================
 */

/* 添加音频流（为播放器创建）
 * @param mixer     混音器
 * @param userdata  用户数据（通常是 VideoState*）
 * @return 音频流指针，失败返回 NULL
 */
AudioStream *audio_mixer_add_stream(AudioMixer *mixer, void *userdata);

/* 移除音频流 */
void audio_mixer_remove_stream(AudioMixer *mixer, AudioStream *stream);

/* 流控制 */
void audio_stream_play(AudioStream *stream);
void audio_stream_pause(AudioStream *stream);
void audio_stream_stop(AudioStream *stream);

/* 设置音量 (0-128) */
void audio_stream_set_volume(AudioStream *stream, int volume);

/* 写入音频数据到流
 * @param stream  音频流
 * @param data    音频数据
 * @param len     数据长度（字节）
 * @return 实际写入的字节数
 */
int audio_stream_write(AudioStream *stream, const uint8_t *data, int len);

/* 获取流缓冲区中的可用数据量（字节）*/
int audio_stream_get_queued(AudioStream *stream);

/* 获取流缓冲区的可写空间（字节）*/
int audio_stream_get_free_space(AudioStream *stream);

/* 清空流缓冲区 */
void audio_stream_flush(AudioStream *stream);

#endif /* FF_AUDIO_MIXER_H */

