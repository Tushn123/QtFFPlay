# Qt 外部渲染方案设计

## 一、可行性分析

### 1.1 当前架构

```
┌────────────────────────────────────────────────────────────────┐
│                    video_refresh (音视频同步控制)               │
│                          │                                     │
│                          ▼ (决定何时显示)                       │
│                    video_display                                │
│                          │                                     │
│        ┌─────────────────┼─────────────────┐                   │
│        ▼                 ▼                 ▼                   │
│  vout_render_begin  video_image_display  vout_render_present   │
│        │                 │                 │                   │
│        │          (获取帧数据 vp->frame)    │                   │
│        │                 │                 │                   │
│        └─────────────────┼─────────────────┘                   │
│                          ▼                                     │
│                   SDL/OpenGL 渲染                               │
└────────────────────────────────────────────────────────────────┘
```

### 1.2 可行性结论：✅ 完全可行

**理由：**

1. **音视频同步与渲染分离**：`video_refresh` 负责同步控制，决定"何时"显示帧；渲染只是"如何"显示帧
2. **帧数据易于获取**：在 `video_image_display` 中，`vp->frame` 包含完整的 AVFrame 数据
3. **渲染逻辑集中**：所有渲染操作都在 `video_display` 函数中，易于添加分支

### 1.3 关键点

- `video_refresh` 计算 `remaining_time`，控制帧显示时机（音视频同步核心）
- 在 `video_display` 中，帧数据已准备好，只需选择渲染方式
- Qt 端收到帧后立即渲染，不破坏同步逻辑

---

## 二、设计方案

### 2.1 架构图

```
┌────────────────────────────────────────────────────────────────┐
│                    video_refresh (音视频同步控制)               │
│                          │                                     │
│                          ▼                                     │
│                    video_display                                │
│                          │                                     │
│         ┌────────────────┼────────────────┐                    │
│         │                │                │                    │
│  [render_mode=SDL]  [render_mode=CALLBACK]                     │
│         │                │                                     │
│         ▼                ▼                                     │
│  SDL/OpenGL 渲染    回调函数传递帧数据                          │
│                          │                                     │
│                          ▼                                     │
│                    Qt OpenGL 渲染                               │
└────────────────────────────────────────────────────────────────┘
```

### 2.2 渲染模式定义

```c
/* 渲染模式 */
typedef enum FFPRenderMode {
    FFP_RENDER_MODE_SDL = 0,      /* SDL 子窗口渲染（默认）*/
    FFP_RENDER_MODE_CALLBACK = 1, /* 回调模式，外部渲染 */
} FFPRenderMode;
```

### 2.3 帧数据结构

```c
/* 传递给外部的视频帧数据 */
typedef struct FFPVideoFrame {
    uint8_t *data[4];      /* YUV/RGB 数据平面 */
    int linesize[4];       /* 每行字节数 */
    int width;             /* 帧宽度 */
    int height;            /* 帧高度 */
    int format;            /* 像素格式 (AVPixelFormat) */
    double pts;            /* 显示时间戳 */
    int64_t pos;           /* 文件位置 */
} FFPVideoFrame;
```

### 2.4 回调函数类型

```c
/* 视频帧回调函数类型 */
typedef void (*ffp_video_frame_callback)(void *opaque, FFPVideoFrame *frame);
```

---

## 三、修改计划

### 3.1 文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `ff_ffplayer.h` | 添加渲染模式、回调类型、帧结构定义 |
| `ff_ffplayer.c` | 添加设置渲染模式和回调的函数 |
| `ffplay.c` | 修改 `video_display`，根据模式选择渲染方式 |
| `mediaplayer.h` | 暴露设置渲染模式和回调的接口 |
| `mediaplayer.c` | 实现接口 |
| `PlayerWidget.cpp` | Qt 端实现回调，OpenGL 渲染 |

### 3.2 详细修改

#### Step 1: ff_ffplayer.h - 添加类型定义

```c
/* 渲染模式 */
typedef enum FFPRenderMode {
    FFP_RENDER_MODE_SDL = 0,      /* SDL 子窗口渲染（默认）*/
    FFP_RENDER_MODE_CALLBACK = 1, /* 回调模式，外部渲染 */
} FFPRenderMode;

/* 传递给外部的视频帧数据 */
typedef struct FFPVideoFrame {
    uint8_t *data[4];      /* YUV/RGB 数据平面 */
    int linesize[4];       /* 每行字节数 */
    int width;             /* 帧宽度 */
    int height;            /* 帧高度 */
    int format;            /* 像素格式 (AVPixelFormat) */
    double pts;            /* 显示时间戳 */
    int64_t pos;           /* 文件位置 */
} FFPVideoFrame;

/* 视频帧回调函数类型 */
typedef void (*ffp_video_frame_callback)(void *opaque, FFPVideoFrame *frame);

/* 在 FFPlayer 结构体中添加 */
typedef struct FFPlayer {
    // ... 现有成员 ...
    
    /* 渲染模式 */
    FFPRenderMode render_mode;
    
    /* 回调渲染相关 */
    ffp_video_frame_callback video_frame_cb;
    void *video_frame_cb_opaque;
} FFPlayer;

/* 设置渲染模式 */
void ffp_set_render_mode(FFPlayer *ffp, FFPRenderMode mode);

/* 设置视频帧回调 */
void ffp_set_video_frame_callback(FFPlayer *ffp, ffp_video_frame_callback cb, void *opaque);
```

#### Step 2: ff_ffplayer.c - 实现设置函数

```c
void ffp_set_render_mode(FFPlayer *ffp, FFPRenderMode mode)
{
    if (!ffp)
        return;
    ffp->render_mode = mode;
    
    /* 回调模式下禁用 SDL 渲染 */
    if (mode == FFP_RENDER_MODE_CALLBACK) {
        ffp->display_disable = 0;  /* 仍需处理显示逻辑，但不用 SDL 渲染 */
    }
}

void ffp_set_video_frame_callback(FFPlayer *ffp, ffp_video_frame_callback cb, void *opaque)
{
    if (!ffp)
        return;
    ffp->video_frame_cb = cb;
    ffp->video_frame_cb_opaque = opaque;
}
```

#### Step 3: ffplay.c - 修改 video_display

```c
void video_display(FFPlayer *ffp, VideoState *is)
{
    static int display_count = 0;
    
    if (!is->width) {
        video_open(ffp, is);
    }

    /* 回调模式：通过回调传递帧数据 */
    if (ffp->render_mode == FFP_RENDER_MODE_CALLBACK) {
        if (is->video_st && ffp->video_frame_cb) {
            Frame *vp = frame_queue_peek_last(&is->pictq);
            if (vp && vp->frame) {
                FFPVideoFrame frame;
                frame.data[0] = vp->frame->data[0];
                frame.data[1] = vp->frame->data[1];
                frame.data[2] = vp->frame->data[2];
                frame.data[3] = vp->frame->data[3];
                frame.linesize[0] = vp->frame->linesize[0];
                frame.linesize[1] = vp->frame->linesize[1];
                frame.linesize[2] = vp->frame->linesize[2];
                frame.linesize[3] = vp->frame->linesize[3];
                frame.width = vp->frame->width;
                frame.height = vp->frame->height;
                frame.format = vp->frame->format;
                frame.pts = vp->pts;
                frame.pos = vp->pos;
                
                ffp->video_frame_cb(ffp->video_frame_cb_opaque, &frame);
            }
        }
        display_count++;
        return;  /* 回调模式直接返回，不执行 SDL 渲染 */
    }

    /* SDL 模式：原有逻辑 */
    vout_render_begin(ffp->vout);
    vout_clear(ffp->vout, 0, 0, 0);
    if (is->audio_st && is->show_mode != SHOW_MODE_VIDEO)
        video_audio_display(ffp, is);
    else if (is->video_st) {
        video_image_display(ffp, is);
    }
    vout_render_present(ffp->vout);
    display_count++;
}
```

#### Step 4: mediaplayer.h/c - 暴露接口

```c
/* mediaplayer.h */
void mp_set_render_mode(MediaPlayer *mp, int mode);
void mp_set_video_frame_callback(MediaPlayer *mp, ffp_video_frame_callback cb, void *opaque);

/* mediaplayer.c */
void mp_set_render_mode(MediaPlayer *mp, int mode)
{
    if (!mp || !mp->ffplayer)
        return;
    ffp_set_render_mode(mp->ffplayer, (FFPRenderMode)mode);
}

void mp_set_video_frame_callback(MediaPlayer *mp, ffp_video_frame_callback cb, void *opaque)
{
    if (!mp || !mp->ffplayer)
        return;
    ffp_set_video_frame_callback(mp->ffplayer, cb, opaque);
}
```

#### Step 5: PlayerWidget.cpp - Qt 端实现

```cpp
// 回调函数（静态，供 C 层调用）
static void on_video_frame(void *opaque, FFPVideoFrame *frame) {
    PlayerWidget *widget = static_cast<PlayerWidget*>(opaque);
    // 发送信号到 Qt 主线程（跨线程安全）
    QMetaObject::invokeMethod(widget, "updateVideoFrame", Qt::QueuedConnection,
        Q_ARG(FFPVideoFrame, *frame));
}

// 初始化时设置回调
void PlayerWidget::initPlayer() {
    // 选择渲染模式
    mp_set_render_mode(m_player, FFP_RENDER_MODE_CALLBACK);
    mp_set_video_frame_callback(m_player, on_video_frame, this);
}

// 接收帧数据并渲染
void PlayerWidget::updateVideoFrame(FFPVideoFrame frame) {
    // 更新 OpenGL 纹理并重绘
    // frame.data, frame.linesize, frame.width, frame.height, frame.format
    update(); // 触发 paintGL
}
```

---

## 四、时序图

### 4.1 SDL 渲染模式（原有）

```
video_refresh
     │
     ▼ (同步控制，决定显示时机)
video_display
     │
     ├─► vout_render_begin
     │
     ├─► video_image_display
     │        │
     │        └─► upload_texture (上传到 GPU)
     │
     └─► vout_render_present (SwapBuffers)
```

### 4.2 回调渲染模式（新增）

```
video_refresh
     │
     ▼ (同步控制，决定显示时机)
video_display
     │
     ├─► 获取 vp->frame
     │
     ├─► 填充 FFPVideoFrame
     │
     └─► video_frame_cb(opaque, &frame)
              │
              ▼ (Qt 主线程)
         PlayerWidget::updateVideoFrame
              │
              ├─► 上传纹理到 Qt OpenGL
              │
              └─► update() → paintGL()
```

---

## 五、注意事项

### 5.1 线程安全

- 回调在 **渲染线程/read_thread** 中调用
- Qt 的 OpenGL 渲染必须在 **Qt 主线程** 中
- 需要通过 `QMetaObject::invokeMethod` 或信号槽跨线程传递

### 5.2 帧数据生命周期

- `vp->frame->data` 指向的内存是 ffmpeg 内部管理的
- 回调返回后，数据可能被覆盖
- **方案 A**：在回调中立即复制数据（推荐）
- **方案 B**：使用 `av_frame_ref` 增加引用计数

### 5.3 像素格式转换

- ffmpeg 解码后通常是 YUV420P
- Qt OpenGL 可直接支持 YUV（使用 shader）
- 或在 C 层用 swscale 转换为 RGB

### 5.4 性能考虑

- 避免在回调中做耗时操作
- 考虑使用 PBO (Pixel Buffer Object) 异步上传
- 可以实现帧缓冲队列，平滑播放

---

## 六、优势

| 特性 | SDL 模式 | 回调模式 |
|------|----------|----------|
| 实现复杂度 | 低 | 中 |
| 与 Qt 集成 | 需要子窗口 | 原生集成 |
| 自定义渲染 | 有限 | 完全可控 |
| 叠加 UI | 困难 | 容易 |
| 音视频同步 | ffplay 控制 | ffplay 控制 |

---

## 七、扩展：音频回调（可选）

同样的模式可以应用于音频：

```c
typedef void (*ffp_audio_frame_callback)(void *opaque, uint8_t *data, int size, int channels, int sample_rate);

void ffp_set_audio_frame_callback(FFPlayer *ffp, ffp_audio_frame_callback cb, void *opaque);
```

这样可以让 Qt 使用自己的音频后端（如 QAudioOutput）播放音频。

---

## 八、实施步骤

1. **Phase 1**：添加类型定义和接口（不影响现有功能）
2. **Phase 2**：实现回调分支（可切换测试）
3. **Phase 3**：Qt 端实现 OpenGL 渲染
4. **Phase 4**：优化和调试

