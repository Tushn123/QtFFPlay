# ijkplayer 迁移文件清单 - 详细操作指南

## 图例说明

- ✅ **直接复制** - 无需任何修改
- 🔧 **参照修改** - 需要小幅修改（改几行代码）
- 🔄 **重构改写** - 需要大幅修改（保留逻辑，改实现）
- 🆕 **全新编写** - 完全新建文件
- ❌ **不需要** - 跳过不复制

---

## 一、核心播放器层 (ijkmedia/ijkplayer/)

### 1.1 主要播放器文件

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ijkplayer.h` | ✅ 直接复制 | 无需修改 | 播放器主API头文件 |
| `ijkplayer.c` | 🔧 参照修改 | 需要改动 5-10 处 | **见下方详细说明** |
| `ijkplayer_internal.h` | ✅ 直接复制 | 无需修改 | 内部结构定义 |

#### `ijkplayer.c` 需要修改的地方：

```c
// 1. 修改日志宏（第 33 行附近）
// 原代码：
#ifndef MPTRACE
#define MPTRACE ALOGD
#endif

// 改为：
#ifdef _WIN32
#include <stdio.h>
#define MPTRACE printf
#define ALOGD printf
#define ALOGE(...) fprintf(stderr, __VA_ARGS__)
#define ALOGW printf
#define ALOGI printf
#else
#define MPTRACE ALOGD
#endif

// 2. 包含头文件（第 24 行附近）
// 添加：
#ifdef _WIN32
#include <windows.h>
#endif

// 3. 其他部分无需修改
```

---

### 1.2 核心播放逻辑

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ff_ffplay.h` | ✅ 直接复制 | 无需修改 | 核心播放头文件 |
| `ff_ffplay.c` | 🔧 参照修改 | 需要改动 10-20 处 | **见下方详细说明** |
| `ff_ffplay_def.h` | ✅ 直接复制 | 无需修改 | 核心结构定义 |
| `ff_ffplay_options.h` | ✅ 直接复制 | 无需修改 | 选项定义 |
| `ff_ffplay_debug.h` | ✅ 直接复制 | 无需修改 | 调试宏 |

#### `ff_ffplay.c` 需要修改的地方：

```c
// 1. 替换 Android 日志（在文件开头）
#ifdef _WIN32
#include <stdio.h>
#define ALOGE(...) fprintf(stderr, __VA_ARGS__)
#define ALOGW printf
#define ALOGI printf
#define ALOGD printf
#define av_log_android(level, ...) av_log(NULL, level, __VA_ARGS__)
#endif

// 2. 视频输出部分（约第 1800 行，video_refresh 函数）
// 原代码使用 SDL_Vout，保持不变，接口已经抽象好

// 3. 音频输出回调（约第 2200 行，audio_decode_frame 函数）
// 保持不变，通过 SDL_Aout 接口

// 4. 平台相关的硬件解码部分（约第 1500 行）
// 查找 mediacodec 相关代码，注释掉或用条件编译包裹：
#if defined(__ANDROID__)
    // mediacodec 相关代码
#endif

// 5. 其他大部分代码无需修改
```

---

### 1.3 消息和管道

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ff_ffmsg.h` | ✅ 直接复制 | 无需修改 | 消息定义 |
| `ff_ffmsg_queue.h` | ✅ 直接复制 | 无需修改 | 消息队列（宏定义） |
| `ff_ffpipeline.h` | ✅ 直接复制 | 无需修改 | 管道接口 |
| `ff_ffpipeline.c` | ✅ 直接复制 | 无需修改 | 管道实现 |
| `ff_ffpipenode.h` | ✅ 直接复制 | 无需修改 | 管道节点接口 |
| `ff_ffpipenode.c` | ✅ 直接复制 | 无需修改 | 管道节点实现 |

---

### 1.4 工具函数

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ff_cmdutils.h` | ✅ 直接复制 | 无需修改 | FFmpeg 命令行工具 |
| `ff_cmdutils.c` | 🔧 参照修改 | 改日志宏 | 替换 ALOGD 为 printf |
| `ff_fferror.h` | ✅ 直接复制 | 无需修改 | 错误码定义 |
| `ff_ffinc.h` | ✅ 直接复制 | 无需修改 | FFmpeg 头文件包含 |
| `ijkmeta.h` | ✅ 直接复制 | 无需修改 | 元数据接口 |
| `ijkmeta.c` | ✅ 直接复制 | 无需修改 | 元数据实现 |
| `config.h` | 🔧 参照修改 | 改配置宏 | **见下方详细说明** |

#### `config.h` 需要修改的地方：

```c
// 添加 Windows 平台定义
#ifdef _WIN32
    #define __WINDOWS__
    #undef __ANDROID__
    #undef __APPLE__
    
    // Windows 日志宏
    #include <stdio.h>
    #define ALOGV(...) printf(__VA_ARGS__)
    #define ALOGD(...) printf(__VA_ARGS__)
    #define ALOGI(...) printf(__VA_ARGS__)
    #define ALOGW(...) printf(__VA_ARGS__)
    #define ALOGE(...) fprintf(stderr, __VA_ARGS__)
    
    // Windows 路径分隔符
    #define PATH_SEPARATOR '\\'
#else
    // 保持原有定义
#endif
```

---

### 1.5 自定义 IO (ijkavformat/)

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ijkavformat.h` | ✅ 直接复制 | 无需修改 | IO 接口头文件 |
| `ijkio.c` | 🔧 参照修改 | 改日志宏 | 替换 Android 日志 |
| `ijkioapplication.h` | ✅ 直接复制 | 无需修改 | 应用层 IO |
| `ijkioapplication.c` | 🔧 参照修改 | 改日志宏 | 替换 Android 日志 |
| `ijkiocache.c` | 🔧 参照修改 | 改日志宏 | 缓存 IO |
| `ijkioandroidio.c` | ❌ 不需要 | - | Android 特定，跳过 |
| `ijkiomanager.h` | ✅ 直接复制 | 无需修改 | IO 管理器 |
| `ijkiomanager.c` | 🔧 参照修改 | 改日志+条件编译 | **见下方详细说明** |
| `ijkioprotocol.h` | ✅ 直接复制 | 无需修改 | 协议接口 |
| `ijkioprotocol.c` | 🔧 参照修改 | 改日志宏 | 替换 Android 日志 |
| `ijkiourlhook.c` | 🔧 参照修改 | 改日志宏 | URL 钩子 |
| `ijklivehook.c` | 🔧 参照修改 | 改日志宏 | 直播钩子 |
| `ijklongurl.c` | ✅ 直接复制 | 无需修改 | 长 URL 处理 |
| `ijkmediadatasource.c` | 🔧 参照修改 | 改日志+条件编译 | **见下方详细说明** |
| `ijksegment.c` | 🔧 参照修改 | 改日志宏 | 分段处理 |
| `ijkurlhook.c` | 🔧 参照修改 | 改日志宏 | URL 钩子 |
| `ijklas.h` | ✅ 直接复制 | 无需修改 | LAS 协议 |
| `ijklas.c` | 🔧 参照修改 | 改日志宏 | LAS 实现 |
| `ijkasync.c` | 🔧 参照修改 | 改日志宏 | 异步 IO |
| `ijkioffio.c` | ✅ 直接复制 | 无需修改 | 文件 IO |
| `allformats.c` | ✅ 直接复制 | 无需修改 | 格式注册 |
| `cJSON.h` | ✅ 直接复制 | 无需修改 | JSON 解析 |
| `cJSON.c` | ✅ 直接复制 | 无需修改 | JSON 实现 |

#### `ijkiomanager.c` 需要修改的地方：

```c
// 1. 替换日志宏（文件开头）
#ifdef _WIN32
#define ALOGE(...) fprintf(stderr, __VA_ARGS__)
#define ALOGD printf
#endif

// 2. 条件编译 Android 特定代码（约第 50 行）
#if defined(__ANDROID__)
    // ijkio_manager_android_io_create() 相关代码
    // 用 #if 包裹，Windows 下不编译
#endif
```

#### `ijkmediadatasource.c` 需要修改的地方：

```c
// 1. 替换日志宏
#ifdef _WIN32
#define ALOGE(...) fprintf(stderr, __VA_ARGS__)
#define ALOGD printf
#endif

// 2. 条件编译 Android JNI 代码
#if defined(__ANDROID__)
    // JNI 相关代码
#endif
```

---

### 1.6 工具库 (ijkavutil/)

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ijkdict.h` | ✅ 直接复制 | 无需修改 | 字典接口 |
| `ijkdict.c` | ✅ 直接复制 | 无需修改 | 字典实现 |
| `ijkfifo.h` | ✅ 直接复制 | 无需修改 | FIFO 接口 |
| `ijkfifo.c` | ✅ 直接复制 | 无需修改 | FIFO 实现 |
| `ijkstl.h` | ✅ 直接复制 | 无需修改 | STL 封装 |
| `ijkstl.cpp` | ✅ 直接复制 | 无需修改 | C++ 实现 |
| `ijkthreadpool.h` | ✅ 直接复制 | 无需修改 | 线程池接口 |
| `ijkthreadpool.c` | ✅ 直接复制 | 无需修改 | 线程池实现 |
| `ijktree.h` | ✅ 直接复制 | 无需修改 | 树结构 |
| `ijktree.c` | ✅ 直接复制 | 无需修改 | 树实现 |
| `ijkutils.h` | ✅ 直接复制 | 无需修改 | 工具函数 |
| `ijkutils.c` | 🔧 参照修改 | 改日志宏 | 替换 ALOGD |
| `opt.h` | ✅ 直接复制 | 无需修改 | 选项定义 |

---

### 1.7 管道实现 (pipeline/)

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ffpipeline_ffplay.h` | ✅ 直接复制 | 无需修改 | 软解管道头文件 |
| `ffpipeline_ffplay.c` | ✅ 直接复制 | 无需修改 | 软解管道实现 |
| `ffpipenode_ffplay_vdec.h` | ✅ 直接复制 | 无需修改 | 软解节点头文件 |
| `ffpipenode_ffplay_vdec.c` | ✅ 直接复制 | 无需修改 | 软解节点实现 |

---

### 1.8 Android 特定文件（不需要）

| 文件/目录 | 状态 | 操作 |
|-----------|------|------|
| `android/` 整个目录 | ❌ 不需要 | 跳过 |
| `ijkplayer_android.h` | ❌ 不需要 | 跳过 |
| `ijkplayer_android.c` | ❌ 不需要 | 跳过 |
| `ijkplayer_jni.c` | ❌ 不需要 | 跳过 |

---

## 二、SDL 抽象层 (ijkmedia/ijksdl/)

### 2.1 核心接口（直接复制）

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ijksdl.h` | ✅ 直接复制 | 无需修改 | SDL 主头文件 |
| `ijksdl_vout.h` | ✅ 直接复制 | 无需修改 | 视频输出接口 |
| `ijksdl_vout.c` | ✅ 直接复制 | 无需修改 | 视频输出公共实现 |
| `ijksdl_vout_internal.h` | ✅ 直接复制 | 无需修改 | 视频输出内部接口 |
| `ijksdl_aout.h` | ✅ 直接复制 | 无需修改 | 音频输出接口 |
| `ijksdl_aout.c` | ✅ 直接复制 | 无需修改 | 音频输出公共实现 |
| `ijksdl_aout_internal.h` | ✅ 直接复制 | 无需修改 | 音频输出内部接口 |
| `ijksdl_audio.h` | ✅ 直接复制 | 无需修改 | 音频定义 |
| `ijksdl_audio.c` | ✅ 直接复制 | 无需修改 | 音频实现 |
| `ijksdl_video.h` | ✅ 直接复制 | 无需修改 | 视频定义 |

---

### 2.2 线程和同步

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ijksdl_thread.h` | ✅ 直接复制 | 无需修改 | 线程接口 |
| `ijksdl_thread.c` | 🔄 重构改写 | 需要重写 | **见下方详细说明** |
| `ijksdl_mutex.h` | ✅ 直接复制 | 无需修改 | 互斥锁接口 |
| `ijksdl_mutex.c` | 🔄 重构改写 | 需要重写 | **见下方详细说明** |
| `ijksdl_timer.h` | ✅ 直接复制 | 无需修改 | 定时器接口 |
| `ijksdl_timer.c` | 🔄 重构改写 | 需要重写 | **见下方详细说明** |

#### `ijksdl_thread.c` 重写方案：

**方案 A: 基于 Windows API**
```c
#include "ijksdl_thread.h"
#include <windows.h>
#include <process.h>

typedef struct SDL_Thread {
    HANDLE handle;
    unsigned thread_id;
    int (*func)(void *);
    void *data;
    char name[32];
} SDL_Thread;

static unsigned __stdcall thread_wrapper(void *arg) {
    SDL_Thread *thread = (SDL_Thread *)arg;
    int ret = thread->func(thread->data);
    return (unsigned)ret;
}

SDL_Thread *SDL_CreateThreadEx(SDL_Thread **thread_out, 
                                int (*fn)(void *), void *data, 
                                const char *name) {
    SDL_Thread *thread = (SDL_Thread *)malloc(sizeof(SDL_Thread));
    if (!thread) return NULL;
    
    thread->func = fn;
    thread->data = data;
    if (name) strncpy(thread->name, name, sizeof(thread->name) - 1);
    
    thread->handle = (HANDLE)_beginthreadex(NULL, 0, thread_wrapper, 
                                             thread, 0, &thread->thread_id);
    if (!thread->handle) {
        free(thread);
        return NULL;
    }
    
    if (thread_out) *thread_out = thread;
    return thread;
}

void SDL_WaitThread(SDL_Thread *thread, int *status) {
    if (!thread) return;
    WaitForSingleObject(thread->handle, INFINITE);
    if (status) GetExitCodeThread(thread->handle, (DWORD *)status);
    CloseHandle(thread->handle);
    free(thread);
}

void SDL_DetachThread(SDL_Thread *thread) {
    if (!thread) return;
    CloseHandle(thread->handle);
    free(thread);
}
```

**方案 B: 基于 pthread-win32（推荐）**
```c
#include "ijksdl_thread.h"
#include <pthread.h>

typedef struct SDL_Thread {
    pthread_t thread;
    int (*func)(void *);
    void *data;
    char name[32];
} SDL_Thread;

static void *thread_wrapper(void *arg) {
    SDL_Thread *thread = (SDL_Thread *)arg;
    int ret = thread->func(thread->data);
    return (void *)(intptr_t)ret;
}

SDL_Thread *SDL_CreateThreadEx(SDL_Thread **thread_out, 
                                int (*fn)(void *), void *data, 
                                const char *name) {
    SDL_Thread *thread = (SDL_Thread *)malloc(sizeof(SDL_Thread));
    if (!thread) return NULL;
    
    thread->func = fn;
    thread->data = data;
    if (name) strncpy(thread->name, name, sizeof(thread->name) - 1);
    
    if (pthread_create(&thread->thread, NULL, thread_wrapper, thread) != 0) {
        free(thread);
        return NULL;
    }
    
    if (thread_out) *thread_out = thread;
    return thread;
}

void SDL_WaitThread(SDL_Thread *thread, int *status) {
    if (!thread) return;
    void *ret;
    pthread_join(thread->thread, &ret);
    if (status) *status = (int)(intptr_t)ret;
    free(thread);
}

void SDL_DetachThread(SDL_Thread *thread) {
    if (!thread) return;
    pthread_detach(thread->thread);
    free(thread);
}
```

#### `ijksdl_mutex.c` 重写方案：

**方案 A: 基于 Windows CRITICAL_SECTION**
```c
#include "ijksdl_mutex.h"
#include <windows.h>

typedef struct SDL_mutex {
    CRITICAL_SECTION cs;
} SDL_mutex;

typedef struct SDL_cond {
    CONDITION_VARIABLE cv;
} SDL_cond;

SDL_mutex *SDL_CreateMutex(void) {
    SDL_mutex *m = (SDL_mutex *)malloc(sizeof(SDL_mutex));
    if (m) InitializeCriticalSection(&m->cs);
    return m;
}

void SDL_DestroyMutex(SDL_mutex *m) {
    if (m) {
        DeleteCriticalSection(&m->cs);
        free(m);
    }
}

int SDL_LockMutex(SDL_mutex *m) {
    if (!m) return -1;
    EnterCriticalSection(&m->cs);
    return 0;
}

int SDL_UnlockMutex(SDL_mutex *m) {
    if (!m) return -1;
    LeaveCriticalSection(&m->cs);
    return 0;
}

SDL_cond *SDL_CreateCond(void) {
    SDL_cond *c = (SDL_cond *)malloc(sizeof(SDL_cond));
    if (c) InitializeConditionVariable(&c->cv);
    return c;
}

void SDL_DestroyCond(SDL_cond *c) {
    if (c) free(c);
}

int SDL_CondWait(SDL_cond *c, SDL_mutex *m) {
    if (!c || !m) return -1;
    return SleepConditionVariableCS(&c->cv, &m->cs, INFINITE) ? 0 : -1;
}

int SDL_CondWaitTimeout(SDL_cond *c, SDL_mutex *m, uint32_t ms) {
    if (!c || !m) return -1;
    return SleepConditionVariableCS(&c->cv, &m->cs, ms) ? 0 : -1;
}

int SDL_CondSignal(SDL_cond *c) {
    if (!c) return -1;
    WakeConditionVariable(&c->cv);
    return 0;
}
```

**方案 B: 基于 pthread-win32（推荐）**
```c
#include "ijksdl_mutex.h"
#include <pthread.h>

typedef struct SDL_mutex {
    pthread_mutex_t mutex;
} SDL_mutex;

typedef struct SDL_cond {
    pthread_cond_t cond;
} SDL_cond;

SDL_mutex *SDL_CreateMutex(void) {
    SDL_mutex *m = (SDL_mutex *)malloc(sizeof(SDL_mutex));
    if (m) pthread_mutex_init(&m->mutex, NULL);
    return m;
}

void SDL_DestroyMutex(SDL_mutex *m) {
    if (m) {
        pthread_mutex_destroy(&m->mutex);
        free(m);
    }
}

int SDL_LockMutex(SDL_mutex *m) {
    return m ? pthread_mutex_lock(&m->mutex) : -1;
}

int SDL_UnlockMutex(SDL_mutex *m) {
    return m ? pthread_mutex_unlock(&m->mutex) : -1;
}

SDL_cond *SDL_CreateCond(void) {
    SDL_cond *c = (SDL_cond *)malloc(sizeof(SDL_cond));
    if (c) pthread_cond_init(&c->cond, NULL);
    return c;
}

void SDL_DestroyCond(SDL_cond *c) {
    if (c) {
        pthread_cond_destroy(&c->cond);
        free(c);
    }
}

int SDL_CondWait(SDL_cond *c, SDL_mutex *m) {
    return (c && m) ? pthread_cond_wait(&c->cond, &m->mutex) : -1;
}

int SDL_CondWaitTimeout(SDL_cond *c, SDL_mutex *m, uint32_t ms) {
    if (!c || !m) return -1;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000;
    return pthread_cond_timedwait(&c->cond, &m->mutex, &ts);
}

int SDL_CondSignal(SDL_cond *c) {
    return c ? pthread_cond_signal(&c->cond) : -1;
}
```

#### `ijksdl_timer.c` 重写方案：

```c
#include "ijksdl_timer.h"
#include <windows.h>

uint64_t SDL_GetTickHR(void) {
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;
    
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000) / freq.QuadPart);
}

void SDL_Delay(uint32_t ms) {
    Sleep(ms);
}

// 其他函数类似实现...
```

---

### 2.3 其他基础文件（直接复制）

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ijksdl_stdinc.h` | ✅ 直接复制 | 无需修改 | 标准库包含 |
| `ijksdl_stdinc.c` | ✅ 直接复制 | 无需修改 | 内存分配等 |
| `ijksdl_log.h` | 🔧 参照修改 | 改日志宏 | Windows 日志输出 |
| `ijksdl_error.h` | ✅ 直接复制 | 无需修改 | 错误处理 |
| `ijksdl_error.c` | ✅ 直接复制 | 无需修改 | 错误实现 |
| `ijksdl_extra_log.h` | ✅ 直接复制 | 无需修改 | 扩展日志 |
| `ijksdl_extra_log.c` | 🔧 参照修改 | 改日志宏 | 替换 Android 日志 |
| `ijksdl_class.h` | ✅ 直接复制 | 无需修改 | 类型定义 |
| `ijksdl_container.h` | ✅ 直接复制 | 无需修改 | 容器宏 |
| `ijksdl_endian.h` | ✅ 直接复制 | 无需修改 | 字节序 |
| `ijksdl_fourcc.h` | ✅ 直接复制 | 无需修改 | FourCC 定义 |
| `ijksdl_misc.h` | ✅ 直接复制 | 无需修改 | 杂项定义 |
| `ijksdl_inc_internal.h` | 🔧 参照修改 | 条件编译 | **见下方详细说明** |

#### `ijksdl_inc_internal.h` 需要修改的地方：

```c
// 添加 Windows 平台判断
#ifdef __WINDOWS__
    #include "windows/ijksdl_inc_internal_windows.h"
#elif defined(__ANDROID__)
    #include "android/ijksdl_inc_internal_android.h"
#elif defined(__APPLE__)
    #include "ios/ijksdl_inc_internal_ios.h"
#else
    #include "dummy/ijksdl_dummy.h"
#endif
```

---

### 2.4 FFmpeg 相关（直接复制）

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ffmpeg/ijksdl_inc_ffmpeg.h` | ✅ 直接复制 | 无需修改 | FFmpeg 头文件 |
| `ffmpeg/ijksdl_image_convert.h` | ✅ 直接复制 | 无需修改 | 图像转换 |
| `ffmpeg/ijksdl_vout_overlay_ffmpeg.h` | ✅ 直接复制 | 无需修改 | Overlay 接口 |
| `ffmpeg/ijksdl_vout_overlay_ffmpeg.c` | ✅ 直接复制 | 无需修改 | Overlay 实现 |
| `ffmpeg/abi_all/image_convert.c` | ✅ 直接复制 | 无需修改 | swscale 封装 |

---

### 2.5 OpenGL ES 渲染器（可选保留）

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `gles2/internal.h` | 🔧 参照修改 | 改 GL 包含 | OpenGL 3.3+ 而非 ES |
| `gles2/renderer.c` | ✅ 直接复制 | 无需修改 | 渲染器入口 |
| `gles2/renderer_yuv420p.c` | ✅ 直接复制 | 无需修改 | YUV420P 渲染 |
| `gles2/renderer_yuv420sp.c` | ✅ 直接复制 | 无需修改 | YUV420SP 渲染 |
| `gles2/renderer_rgb.c` | ✅ 直接复制 | 无需修改 | RGB 渲染 |
| `gles2/shader.c` | ✅ 直接复制 | 无需修改 | 着色器管理 |
| `gles2/color.c` | ✅ 直接复制 | 无需修改 | 颜色转换 |
| `gles2/common.c` | ✅ 直接复制 | 无需修改 | 公共函数 |
| `gles2/fsh/*.fsh.c` | ✅ 直接复制 | 无需修改 | 片段着色器 |
| `gles2/vsh/*.vsh.c` | ✅ 直接复制 | 无需修改 | 顶点着色器 |

#### `gles2/internal.h` 需要修改的地方：

```c
// 原代码：
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// 改为（Windows OpenGL）：
#ifdef _WIN32
    #include <glad/glad.h>  // 或 GLEW
    // #include <GL/gl.h>
#else
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#endif
```

---

### 2.6 EGL 支持（可选）

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `ijksdl_egl.h` | 🔧 参照修改 | Qt 替代 | Qt 处理 OpenGL 上下文 |
| `ijksdl_egl.c` | 🔧 参照修改 | Qt 替代 | 可不用，Qt 自带管理 |
| `ijksdl_gles2.h` | 🔧 参照修改 | 改包含 | OpenGL 3.3 头文件 |

---

### 2.7 Dummy 实现（保留用于测试）

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `dummy/ijksdl_dummy.h` | ✅ 直接复制 | 无需修改 | 空实现头文件 |
| `dummy/ijksdl_vout_dummy.h` | ✅ 直接复制 | 无需修改 | 空视频输出 |
| `dummy/ijksdl_vout_dummy.c` | ✅ 直接复制 | 无需修改 | 空实现（调试用） |

---

### 2.8 Android 特定文件（不需要）

| 文件/目录 | 状态 | 操作 |
|-----------|------|------|
| `android/` 整个目录 | ❌ 不需要 | 跳过 |

---

### 2.9 Windows 平台实现（新建）

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `windows/ijksdl_inc_internal_windows.h` | 🆕 全新编写 | 新建 | Windows 内部头文件 |
| `windows/ijksdl_vout_qt_opengl.h` | 🆕 全新编写 | 新建 | **见完整代码示例** |
| `windows/ijksdl_vout_qt_opengl.cpp` | 🆕 全新编写 | 新建 | Qt OpenGL 视频渲染 |
| `windows/ijksdl_aout_qt_audio.h` | 🆕 全新编写 | 新建 | **见完整代码示例** |
| `windows/ijksdl_aout_qt_audio.cpp` | 🆕 全新编写 | 新建 | Qt 音频输出 |

这些文件的完整代码已在之前的 `ijkplayer_to_qt_windows_migration_guide.md` 中提供。

---

## 三、Qt 封装层（全新编写）

| 文件 | 状态 | 操作 | 说明 |
|------|------|------|------|
| `qt-wrapper/IJKMediaPlayer.h` | 🆕 全新编写 | 新建 | Qt 播放器类头文件 |
| `qt-wrapper/IJKMediaPlayer.cpp` | 🆕 全新编写 | 新建 | Qt 播放器类实现 |
| `qt-wrapper/IJKVideoWidget.h` | 🆕 全新编写 | 新建 | 视频显示控件头文件 |
| `qt-wrapper/IJKVideoWidget.cpp` | 🆕 全新编写 | 新建 | 视频显示控件实现 |

代码示例已在之前的文档中提供。

---

## 四、文件处理总结

### 统计数据

| 类别 | 数量 | 说明 |
|------|------|------|
| ✅ 直接复制（无需修改） | **约 60 个文件** | 占 70% |
| 🔧 参照修改（小改） | **约 20 个文件** | 占 23% |
| 🔄 重构改写（大改） | **3 个文件** | `ijksdl_thread/mutex/timer.c` |
| 🆕 全新编写 | **5-7 个文件** | Windows SDL 层 + Qt 封装 |
| ❌ 不需要 | **约 15 个文件** | Android/iOS 特定代码 |

### 修改工作量估算

| 任务 | 文件数 | 预计时间 | 说明 |
|------|--------|----------|------|
| 直接复制文件 | 60 | 1 小时 | 简单复制粘贴 |
| 修改日志宏 | 20 | 2-3 小时 | 查找替换 ALOGD 等 |
| 重写线程/互斥锁 | 3 | 1-2 天 | 选择 pthread-win32 更快 |
| 新建 SDL Windows 层 | 4 | 3-5 天 | OpenGL + 音频输出 |
| 新建 Qt 封装层 | 2 | 2-3 天 | QObject 类 |
| 调试和测试 | - | 5-7 天 | 音视频同步等 |
| **总计** | **~90 个文件** | **15-23 天** | 约 3-4 周 |

---

## 五、详细操作步骤

### 第 1 步：创建项目结构（30 分钟）

```bash
mkdir ijkplayer-qt
cd ijkplayer-qt

# 创建目录
mkdir -p ijkmedia/ijkplayer
mkdir -p ijkmedia/ijksdl/windows
mkdir -p ijkmedia/ijksdl/ffmpeg
mkdir -p ijkmedia/ijksdl/gles2
mkdir -p ijkmedia/ijksdl/dummy
mkdir -p qt-wrapper
mkdir -p examples/SimplePlayer
mkdir -p 3rdparty/ffmpeg
mkdir -p 3rdparty/pthread-win32
```

### 第 2 步：复制核心播放器文件（30 分钟）

```bash
cd /path/to/ijkplayer-master

# 复制 ijkplayer 核心
cp ijkmedia/ijkplayer/*.h ijkplayer-qt/ijkmedia/ijkplayer/
cp ijkmedia/ijkplayer/*.c ijkplayer-qt/ijkmedia/ijkplayer/
cp -r ijkmedia/ijkplayer/ijkavformat ijkplayer-qt/ijkmedia/ijkplayer/
cp -r ijkmedia/ijkplayer/ijkavutil ijkplayer-qt/ijkmedia/ijkplayer/
cp -r ijkmedia/ijkplayer/pipeline ijkplayer-qt/ijkmedia/ijkplayer/

# 删除 Android 特定文件
rm -rf ijkplayer-qt/ijkmedia/ijkplayer/android
```

### 第 3 步：复制 SDL 基础文件（30 分钟）

```bash
# 复制 SDL 接口和公共实现
cp ijkmedia/ijksdl/*.h ijkplayer-qt/ijkmedia/ijksdl/
cp ijkmedia/ijksdl/*.c ijkplayer-qt/ijkmedia/ijksdl/

# 复制 FFmpeg 相关
cp -r ijkmedia/ijksdl/ffmpeg ijkplayer-qt/ijkmedia/ijksdl/

# 复制 OpenGL 渲染器（可选）
cp -r ijkmedia/ijksdl/gles2 ijkplayer-qt/ijkmedia/ijksdl/

# 复制 dummy 实现
cp -r ijkmedia/ijksdl/dummy ijkplayer-qt/ijkmedia/ijksdl/

# 删除 Android 特定实现
rm -rf ijkplayer-qt/ijkmedia/ijksdl/android
```

### 第 4 步：批量修改日志宏（2-3 小时）

在 `ijkplayer-qt/` 目录下执行：

#### Windows PowerShell 脚本：
```powershell
# 创建一个 PowerShell 脚本：replace_log.ps1

Get-ChildItem -Path . -Include *.c,*.cpp,*.h -Recurse | ForEach-Object {
    $content = Get-Content $_.FullName
    
    # 在文件开头添加 Windows 日志宏定义
    if ($content -notmatch "#ifdef _WIN32") {
        $header = @"
#ifdef _WIN32
#include <stdio.h>
#define ALOGV(...) printf(__VA_ARGS__)
#define ALOGD(...) printf(__VA_ARGS__)
#define ALOGI(...) printf(__VA_ARGS__)
#define ALOGW(...) printf(__VA_ARGS__)
#define ALOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

"@
        $newContent = $header + ($content -join "`n")
        Set-Content -Path $_.FullName -Value $newContent
    }
}
```

#### Linux/Mac Bash 脚本：
```bash
#!/bin/bash
# replace_log.sh

find ijkmedia -name "*.c" -o -name "*.cpp" -o -name "*.h" | while read file; do
    # 检查是否已经有 Windows 宏定义
    if ! grep -q "#ifdef _WIN32" "$file"; then
        # 在文件开头添加宏定义
        cat > tmp_file << 'EOF'
#ifdef _WIN32
#include <stdio.h>
#define ALOGV(...) printf(__VA_ARGS__)
#define ALOGD(...) printf(__VA_ARGS__)
#define ALOGI(...) printf(__VA_ARGS__)
#define ALOGW(...) printf(__VA_ARGS__)
#define ALOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

EOF
        cat "$file" >> tmp_file
        mv tmp_file "$file"
    fi
done
```

### 第 5 步：创建 Windows SDL 层（3-5 天）

参考之前提供的完整代码创建以下文件：
- `ijkmedia/ijksdl/windows/ijksdl_vout_qt_opengl.h/.cpp`
- `ijkmedia/ijksdl/windows/ijksdl_aout_qt_audio.h/.cpp`

### 第 6 步：重写线程和互斥锁（1-2 天）

使用 pthread-win32 实现（推荐）或 Windows API。

### 第 7 步：创建 Qt 封装层（2-3 天）

参考之前提供的代码创建：
- `qt-wrapper/IJKMediaPlayer.h/.cpp`
- `qt-wrapper/IJKVideoWidget.h/.cpp`

### 第 8 步：编写 CMakeLists.txt（1 天）

### 第 9 步：编译和调试（5-7 天）

---

## 六、快速参考表

### 需要手动修改的关键点

| 文件 | 修改内容 | 行数 | 难度 |
|------|----------|------|------|
| `ijkplayer.c` | 日志宏 | 3-5 处 | ⭐ 简单 |
| `ff_ffplay.c` | 日志宏 + 条件编译 | 10-15 处 | ⭐⭐ 中等 |
| `config.h` | 平台宏定义 | 20-30 行 | ⭐ 简单 |
| `ijkiomanager.c` | 条件编译 Android 代码 | 5-10 处 | ⭐⭐ 中等 |
| `ijksdl_thread.c` | 完全重写 | 200+ 行 | ⭐⭐⭐ 困难 |
| `ijksdl_mutex.c` | 完全重写 | 150+ 行 | ⭐⭐⭐ 困难 |
| `ijksdl_timer.c` | 完全重写 | 100+ 行 | ⭐⭐ 中等 |

### 代码搜索关键字

需要批量替换的内容：

```bash
# 搜索需要替换的内容
grep -r "ALOGD" ijkmedia/  # 日志宏
grep -r "__ANDROID__" ijkmedia/  # Android 特定代码
grep -r "J4AC_" ijkmedia/  # JNI 调用
grep -r "mediacodec" ijkmedia/  # 硬件解码
```

---

## 总结

### ✅ 可以直接复制使用的文件（约 70%）：
- 所有 `ijkplayer/` 核心播放逻辑
- 所有 `ijkavformat/` 和 `ijkavutil/`
- 大部分 `ijksdl/` 的接口定义和公共实现
- OpenGL 渲染器代码（gles2/）

### 🔧 需要小幅修改的文件（约 23%）：
- 替换日志宏（ALOGD → printf）
- 条件编译 Android 特定代码
- 大约 20 个文件，每个文件改 3-10 处

### 🔄 需要重写的文件（3 个）：
- `ijksdl_thread.c` - 线程封装
- `ijksdl_mutex.c` - 互斥锁封装
- `ijksdl_timer.c` - 定时器封装

### 🆕 需要新建的文件（5-7 个）：
- Windows SDL 层实现（4 个文件）
- Qt 封装层（2-4 个文件）

**关键建议**：
1. 优先使用 **pthread-win32** 库，可节省 1-2 天时间
2. 使用脚本批量处理日志宏替换
3. OpenGL 渲染器代码基本可以直接用
4. 核心播放逻辑无需修改，非常稳定

移植工作的 **80% 是简单复制粘贴**，**15% 是查找替换**，**5% 是核心代码编写**。


