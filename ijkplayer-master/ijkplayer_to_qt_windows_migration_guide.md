# ijkplayer 移植到 Qt Windows 详细指南

## 一、项目结构分析

### ijkplayer 原有架构
ijkplayer 是B站开源的基于 FFmpeg 的跨平台播放器，采用三层架构：

1. **核心播放器层** (`ijkmedia/ijkplayer/`)
   - 平台无关的播放逻辑
   - 基于 FFmpeg 的 ffplay 改造
   - 提供标准的播放器 API（play, pause, seek等）

2. **SDL抽象层** (`ijkmedia/ijksdl/`)
   - 音频输出抽象 (SDL_Aout)
   - 视频输出抽象 (SDL_Vout)
   - 线程、互斥锁等系统封装

3. **平台绑定层** (`android/`, `ios/`)
   - Android: JNI + Java MediaPlayer API
   - iOS: Objective-C + MediaPlayer Framework

### 核心依赖
- **FFmpeg**: 音视频解码、解封装
- **pthread**: 线程管理
- **OpenGL/OpenGL ES**: 视频渲染（可选）

---

## 二、移植方案设计

### 总体思路
1. **保留**: 核心播放器逻辑（`ijkmedia/ijkplayer/`）
2. **重写**: SDL层的Windows实现（音视频输出）
3. **新增**: Qt界面层和C++封装
4. **编译**: Windows版FFmpeg库

### 新项目结构

```
ijkplayer-qt/
├── 3rdparty/                      # 第三方库
│   ├── ffmpeg/                    # FFmpeg Windows编译结果
│   │   ├── include/
│   │   └── lib/
│   └── pthread-win32/             # pthread Windows实现
│       ├── include/
│       └── lib/
│
├── ijkmedia/                      # 核心播放器（从原项目复制）
│   ├── ijkplayer/                 # 播放器核心
│   │   ├── ff_ffplay.c/h          # 主播放逻辑
│   │   ├── ijkplayer.c/h          # 播放器API
│   │   ├── ff_ffpipeline.c/h
│   │   ├── ijkavformat/           # 自定义IO
│   │   ├── ijkavutil/             # 工具类
│   │   └── pipeline/
│   │
│   └── ijksdl/                    # SDL抽象层
│       ├── ijksdl_vout.h          # 视频输出接口（保留）
│       ├── ijksdl_aout.h          # 音频输出接口（保留）
│       ├── ijksdl_thread.c/h      # 线程封装（保留）
│       ├── ijksdl_mutex.c/h       # 互斥锁封装（保留）
│       │
│       ├── windows/               # Windows平台实现（新增）
│       │   ├── ijksdl_vout_qt_opengl.cpp     # Qt OpenGL视频渲染
│       │   ├── ijksdl_aout_qt_audio.cpp      # Qt音频输出
│       │   └── ijksdl_inc_internal_windows.h
│       │
│       └── dummy/                 # 保留空实现（用于调试）
│
├── qt-wrapper/                    # Qt封装层（新增）
│   ├── IJKMediaPlayer.h/cpp       # C++播放器类
│   ├── IJKVideoWidget.h/cpp       # Qt视频显示控件
│   └── IJKAudioOutput.h/cpp       # Qt音频输出封装
│
├── examples/                      # 示例程序
│   └── SimplePlayer/
│       ├── main.cpp
│       ├── MainWindow.h/cpp       # 主窗口
│       ├── MainWindow.ui
│       └── SimplePlayer.pro       # qmake项目文件
│
├── CMakeLists.txt                 # CMake构建文件
└── README.md
```

---

## 三、详细移植步骤

### Step 1: 准备开发环境

#### 1.1 安装必要工具
```bash
# Windows 环境需要：
- Visual Studio 2019+ (MSVC编译器)
- CMake 3.16+
- Qt 5.15+ 或 Qt 6.x
- Git
- MSYS2 (用于编译FFmpeg)
```

#### 1.2 编译 FFmpeg for Windows
```bash
# 使用 MSYS2 MinGW64 环境
pacman -S mingw-w64-x86_64-toolchain
pacman -S yasm nasm

# 下载 FFmpeg 源码
git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg

# 配置编译选项（简化版）
./configure \
  --prefix=/mingw64 \
  --arch=x86_64 \
  --target-os=mingw32 \
  --enable-shared \
  --disable-static \
  --enable-gpl \
  --enable-version3 \
  --disable-doc \
  --disable-debug \
  --enable-avresample

# 编译安装
make -j8
make install
```

或者直接下载预编译版本：https://github.com/BtbN/FFmpeg-Builds/releases

#### 1.3 准备 pthread-win32
```bash
# 下载 pthreads-win32
# https://sourceware.org/pthreads-win32/

# 或使用 vcpkg
vcpkg install pthreads:x64-windows
```

---

### Step 2: 复制核心代码

```bash
# 从原 ijkplayer 项目复制核心代码
mkdir -p ijkplayer-qt/ijkmedia

# 复制播放器核心（平台无关代码）
cp -r ijkplayer-master/ijkmedia/ijkplayer ijkplayer-qt/ijkmedia/

# 复制SDL抽象层基础代码
mkdir -p ijkplayer-qt/ijkmedia/ijksdl
cp ijkplayer-master/ijkmedia/ijksdl/*.h ijkplayer-qt/ijkmedia/ijksdl/
cp ijkplayer-master/ijkmedia/ijksdl/*.c ijkplayer-qt/ijkmedia/ijksdl/
cp -r ijkplayer-master/ijkmedia/ijksdl/ffmpeg ijkplayer-qt/ijkmedia/ijksdl/
cp -r ijkplayer-master/ijkmedia/ijksdl/gles2 ijkplayer-qt/ijkmedia/ijksdl/
cp -r ijkplayer-master/ijkmedia/ijksdl/dummy ijkplayer-qt/ijkmedia/ijksdl/
```

---

### Step 3: 实现 Windows SDL 层

#### 3.1 视频输出实现 - `ijksdl_vout_qt_opengl.cpp`

基于 Qt 的 QOpenGLWidget 或 QOpenGLWindow 实现视频渲染：

```cpp
// ijkmedia/ijksdl/windows/ijksdl_vout_qt_opengl.h
#ifndef IJKSDL_VOUT_QT_OPENGL_H
#define IJKSDL_VOUT_QT_OPENGL_H

#include "../ijksdl_vout.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>

class IJKVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit IJKVideoWidget(QWidget *parent = nullptr);
    ~IJKVideoWidget();
    
    // SDL_Vout 接口
    void displayYUVFrame(uint8_t *yData, uint8_t *uData, uint8_t *vData,
                         int width, int height,
                         int yLinesize, int uLinesize, int vLinesize);
    
protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    
private:
    QOpenGLShaderProgram *m_program;
    QOpenGLTexture *m_textureY;
    QOpenGLTexture *m_textureU;
    QOpenGLTexture *m_textureV;
    
    GLuint m_textures[3];
    int m_videoWidth;
    int m_videoHeight;
};

// C接口封装
extern "C" {
    SDL_Vout* SDL_VoutQt_Create(void* qtWidget);
    void SDL_VoutQt_Display(SDL_Vout *vout, SDL_VoutOverlay *overlay);
}

#endif
```

```cpp
// ijkmedia/ijksdl/windows/ijksdl_vout_qt_opengl.cpp
#include "ijksdl_vout_qt_opengl.h"
#include <QDebug>

// YUV420P 顶点着色器
static const char *vertexShaderSource = R"(
    attribute vec4 vertexIn;
    attribute vec2 textureIn;
    varying vec2 textureOut;
    void main(void) {
        gl_Position = vertexIn;
        textureOut = textureIn;
    }
)";

// YUV420P 片段着色器
static const char *fragmentShaderSource = R"(
    varying vec2 textureOut;
    uniform sampler2D textureY;
    uniform sampler2D textureU;
    uniform sampler2D textureV;
    void main(void) {
        vec3 yuv;
        vec3 rgb;
        yuv.x = texture2D(textureY, textureOut).r;
        yuv.y = texture2D(textureU, textureOut).r - 0.5;
        yuv.z = texture2D(textureV, textureOut).r - 0.5;
        
        rgb = mat3(1.0, 1.0, 1.0,
                   0.0, -0.39465, 2.03211,
                   1.13983, -0.58060, 0.0) * yuv;
        gl_FragColor = vec4(rgb, 1.0);
    }
)";

IJKVideoWidget::IJKVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_program(nullptr)
    , m_videoWidth(0)
    , m_videoHeight(0) {
    memset(m_textures, 0, sizeof(m_textures));
}

void IJKVideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    // 创建着色器程序
    m_program = new QOpenGLShaderProgram(this);
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    m_program->link();
    m_program->bind();
    
    // 创建纹理
    glGenTextures(3, m_textures);
    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, m_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void IJKVideoWidget::displayYUVFrame(uint8_t *yData, uint8_t *uData, uint8_t *vData,
                                      int width, int height,
                                      int yLinesize, int uLinesize, int vLinesize) {
    m_videoWidth = width;
    m_videoHeight = height;
    
    makeCurrent();
    
    // 更新 Y 纹理
    glBindTexture(GL_TEXTURE_2D, m_textures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, yData);
    
    // 更新 U 纹理
    glBindTexture(GL_TEXTURE_2D, m_textures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width/2, height/2, 0, GL_RED, GL_UNSIGNED_BYTE, uData);
    
    // 更新 V 纹理
    glBindTexture(GL_TEXTURE_2D, m_textures[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width/2, height/2, 0, GL_RED, GL_UNSIGNED_BYTE, vData);
    
    doneCurrent();
    update(); // 触发重绘
}

void IJKVideoWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (m_videoWidth <= 0 || m_videoHeight <= 0) {
        return;
    }
    
    m_program->bind();
    
    // 绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textures[0]);
    m_program->setUniformValue("textureY", 0);
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textures[1]);
    m_program->setUniformValue("textureU", 1);
    
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_textures[2]);
    m_program->setUniformValue("textureV", 2);
    
    // 绘制矩形
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };
    static const GLfloat texCoords[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f
    };
    
    m_program->setAttributeArray("vertexIn", vertices, 2);
    m_program->setAttributeArray("textureIn", texCoords, 2);
    m_program->enableAttributeArray("vertexIn");
    m_program->enableAttributeArray("textureIn");
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    m_program->disableAttributeArray("vertexIn");
    m_program->disableAttributeArray("textureIn");
}

// C接口封装
typedef struct SDL_Vout_Opaque {
    IJKVideoWidget *widget;
} SDL_Vout_Opaque;

static SDL_VoutOverlay* func_create_overlay(int width, int height, int format, SDL_Vout *vout) {
    // 创建 overlay 结构
    // 实现略...
    return nullptr;
}

static void func_free_l(SDL_Vout *vout) {
    if (!vout) return;
    SDL_Vout_Opaque *opaque = (SDL_Vout_Opaque *)vout->opaque;
    if (opaque) {
        delete opaque->widget;
        free(opaque);
    }
    free(vout);
}

static int func_display_overlay(SDL_Vout *vout, SDL_VoutOverlay *overlay) {
    SDL_Vout_Opaque *opaque = (SDL_Vout_Opaque *)vout->opaque;
    if (!opaque || !opaque->widget || !overlay) {
        return -1;
    }
    
    // 调用 Qt Widget 显示
    opaque->widget->displayYUVFrame(
        overlay->pixels[0],
        overlay->pixels[1],
        overlay->pixels[2],
        overlay->w, overlay->h,
        overlay->pitches[0],
        overlay->pitches[1],
        overlay->pitches[2]
    );
    
    return 0;
}

extern "C" SDL_Vout* SDL_VoutQt_Create(void* qtWidget) {
    SDL_Vout *vout = (SDL_Vout *)calloc(1, sizeof(SDL_Vout));
    if (!vout) return nullptr;
    
    SDL_Vout_Opaque *opaque = (SDL_Vout_Opaque *)calloc(1, sizeof(SDL_Vout_Opaque));
    if (!opaque) {
        free(vout);
        return nullptr;
    }
    
    if (qtWidget) {
        opaque->widget = (IJKVideoWidget *)qtWidget;
    } else {
        opaque->widget = new IJKVideoWidget();
    }
    
    vout->opaque = opaque;
    vout->create_overlay = func_create_overlay;
    vout->free_l = func_free_l;
    vout->display_overlay = func_display_overlay;
    
    return vout;
}
```

#### 3.2 音频输出实现 - `ijksdl_aout_qt_audio.cpp`

基于 Qt Multimedia 的 QAudioOutput：

```cpp
// ijkmedia/ijksdl/windows/ijksdl_aout_qt_audio.h
#ifndef IJKSDL_AOUT_QT_AUDIO_H
#define IJKSDL_AOUT_QT_AUDIO_H

#include "../ijksdl_aout.h"
#include <QAudioOutput>
#include <QIODevice>

class IJKAudioOutput : public QObject {
    Q_OBJECT
public:
    explicit IJKAudioOutput(QObject *parent = nullptr);
    ~IJKAudioOutput();
    
    int open(const SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
    void pause(bool pauseOn);
    void flush();
    void setVolume(float left, float right);
    void close();
    
    int write(uint8_t *data, int len);
    
private:
    QAudioOutput *m_audioOutput;
    QIODevice *m_audioDevice;
    QAudioFormat m_format;
};

extern "C" {
    SDL_Aout* SDL_AoutQt_Create();
}

#endif
```

```cpp
// ijkmedia/ijksdl/windows/ijksdl_aout_qt_audio.cpp
#include "ijksdl_aout_qt_audio.h"
#include <QDebug>

IJKAudioOutput::IJKAudioOutput(QObject *parent)
    : QObject(parent)
    , m_audioOutput(nullptr)
    , m_audioDevice(nullptr) {
}

IJKAudioOutput::~IJKAudioOutput() {
    close();
}

int IJKAudioOutput::open(const SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    close();
    
    // 设置音频格式
    m_format.setSampleRate(desired->freq);
    m_format.setChannelCount(desired->channels);
    m_format.setSampleSize(16); // 16-bit
    m_format.setCodec("audio/pcm");
    m_format.setByteOrder(QAudioFormat::LittleEndian);
    m_format.setSampleType(QAudioFormat::SignedInt);
    
    QAudioDeviceInfo info = QAudioDeviceInfo::defaultOutputDevice();
    if (!info.isFormatSupported(m_format)) {
        qWarning() << "Raw audio format not supported by backend, cannot play audio.";
        m_format = info.nearestFormat(m_format);
    }
    
    m_audioOutput = new QAudioOutput(m_format, this);
    m_audioDevice = m_audioOutput->start();
    
    if (!m_audioDevice) {
        qWarning() << "Failed to start audio output";
        return -1;
    }
    
    // 填充 obtained
    if (obtained) {
        obtained->freq = m_format.sampleRate();
        obtained->channels = m_format.channelCount();
        obtained->format = AUDIO_S16SYS; // 16-bit signed
        obtained->samples = desired->samples;
        obtained->size = desired->samples * m_format.channelCount() * 2;
    }
    
    return 0;
}

void IJKAudioOutput::pause(bool pauseOn) {
    if (m_audioOutput) {
        if (pauseOn) {
            m_audioOutput->suspend();
        } else {
            m_audioOutput->resume();
        }
    }
}

void IJKAudioOutput::flush() {
    if (m_audioOutput) {
        m_audioOutput->reset();
    }
}

void IJKAudioOutput::setVolume(float left, float right) {
    if (m_audioOutput) {
        float volume = (left + right) / 2.0f;
        m_audioOutput->setVolume(volume);
    }
}

void IJKAudioOutput::close() {
    if (m_audioOutput) {
        m_audioOutput->stop();
        delete m_audioOutput;
        m_audioOutput = nullptr;
        m_audioDevice = nullptr;
    }
}

int IJKAudioOutput::write(uint8_t *data, int len) {
    if (m_audioDevice) {
        return m_audioDevice->write((const char*)data, len);
    }
    return -1;
}

// C接口封装
typedef struct SDL_Aout_Opaque {
    IJKAudioOutput *output;
} SDL_Aout_Opaque;

static int func_open_audio(SDL_Aout *aout, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    SDL_Aout_Opaque *opaque = (SDL_Aout_Opaque *)aout->opaque;
    return opaque->output->open(desired, obtained);
}

static void func_pause_audio(SDL_Aout *aout, int pause_on) {
    SDL_Aout_Opaque *opaque = (SDL_Aout_Opaque *)aout->opaque;
    opaque->output->pause(pause_on != 0);
}

static void func_flush_audio(SDL_Aout *aout) {
    SDL_Aout_Opaque *opaque = (SDL_Aout_Opaque *)aout->opaque;
    opaque->output->flush();
}

static void func_set_volume(SDL_Aout *aout, float left, float right) {
    SDL_Aout_Opaque *opaque = (SDL_Aout_Opaque *)aout->opaque;
    opaque->output->setVolume(left, right);
}

static void func_close_audio(SDL_Aout *aout) {
    SDL_Aout_Opaque *opaque = (SDL_Aout_Opaque *)aout->opaque;
    opaque->output->close();
}

static void func_free_l(SDL_Aout *aout) {
    if (!aout) return;
    SDL_Aout_Opaque *opaque = (SDL_Aout_Opaque *)aout->opaque;
    if (opaque) {
        delete opaque->output;
        free(opaque);
    }
    free(aout);
}

extern "C" SDL_Aout* SDL_AoutQt_Create() {
    SDL_Aout *aout = (SDL_Aout *)calloc(1, sizeof(SDL_Aout));
    if (!aout) return nullptr;
    
    SDL_Aout_Opaque *opaque = (SDL_Aout_Opaque *)calloc(1, sizeof(SDL_Aout_Opaque));
    if (!opaque) {
        free(aout);
        return nullptr;
    }
    
    opaque->output = new IJKAudioOutput();
    
    aout->opaque = opaque;
    aout->open_audio = func_open_audio;
    aout->pause_audio = func_pause_audio;
    aout->flush_audio = func_flush_audio;
    aout->set_volume = func_set_volume;
    aout->close_audio = func_close_audio;
    aout->free_l = func_free_l;
    
    return aout;
}
```

---

### Step 4: 创建 Qt 封装层

```cpp
// qt-wrapper/IJKMediaPlayer.h
#ifndef IJKMEDIAPLAYER_H
#define IJKMEDIAPLAYER_H

#include <QObject>
#include <QString>
#include "../ijkmedia/ijkplayer/ijkplayer.h"

class IJKVideoWidget;

class IJKMediaPlayer : public QObject {
    Q_OBJECT
    
public:
    enum State {
        Idle = MP_STATE_IDLE,
        Initialized = MP_STATE_INITIALIZED,
        Preparing = MP_STATE_ASYNC_PREPARING,
        Prepared = MP_STATE_PREPARED,
        Started = MP_STATE_STARTED,
        Paused = MP_STATE_PAUSED,
        Completed = MP_STATE_COMPLETED,
        Stopped = MP_STATE_STOPPED,
        Error = MP_STATE_ERROR,
        End = MP_STATE_END
    };
    Q_ENUM(State)
    
    explicit IJKMediaPlayer(QObject *parent = nullptr);
    ~IJKMediaPlayer();
    
    // 播放控制
    void setDataSource(const QString &url);
    void prepareAsync();
    void start();
    void pause();
    void stop();
    void seekTo(qint64 msec);
    
    // 状态查询
    State state() const;
    bool isPlaying() const;
    qint64 currentPosition() const;
    qint64 duration() const;
    
    // 设置选项
    void setOption(int category, const QString &name, const QString &value);
    void setOptionInt(int category, const QString &name, qint64 value);
    
    // 设置视频显示控件
    void setVideoWidget(IJKVideoWidget *widget);
    
signals:
    void prepared();
    void started();
    void paused();
    void completed();
    void error(int code);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    
private slots:
    void onMessageLoop();
    
private:
    static int messageLoop(void *arg);
    
    IjkMediaPlayer *m_player;
    IJKVideoWidget *m_videoWidget;
    QTimer *m_msgTimer;
};

#endif
```

```cpp
// qt-wrapper/IJKMediaPlayer.cpp
#include "IJKMediaPlayer.h"
#include "IJKVideoWidget.h"
#include <QTimer>
#include <QDebug>

IJKMediaPlayer::IJKMediaPlayer(QObject *parent)
    : QObject(parent)
    , m_player(nullptr)
    , m_videoWidget(nullptr)
    , m_msgTimer(new QTimer(this)) {
    
    // 初始化 ijkplayer
    ijkmp_global_init();
    
    // 创建播放器
    m_player = ijkmp_create(messageLoop);
    ijkmp_set_weak_thiz(m_player, this);
    
    // 设置默认选项
    ijkmp_set_option_int(m_player, IJKMP_OPT_CATEGORY_PLAYER, "start-on-prepared", 0);
    ijkmp_set_option_int(m_player, IJKMP_OPT_CATEGORY_PLAYER, "mediacodec", 0);
    
    // 消息循环定时器
    connect(m_msgTimer, &QTimer::timeout, this, &IJKMediaPlayer::onMessageLoop);
}

IJKMediaPlayer::~IJKMediaPlayer() {
    if (m_player) {
        ijkmp_shutdown(m_player);
        ijkmp_dec_ref_p(&m_player);
    }
    ijkmp_global_uninit();
}

void IJKMediaPlayer::setDataSource(const QString &url) {
    if (m_player) {
        ijkmp_set_data_source(m_player, url.toUtf8().constData());
    }
}

void IJKMediaPlayer::prepareAsync() {
    if (m_player) {
        ijkmp_prepare_async(m_player);
        m_msgTimer->start(10); // 每10ms检查一次消息
    }
}

void IJKMediaPlayer::start() {
    if (m_player) {
        ijkmp_start(m_player);
        emit started();
    }
}

void IJKMediaPlayer::pause() {
    if (m_player) {
        ijkmp_pause(m_player);
        emit paused();
    }
}

void IJKMediaPlayer::stop() {
    if (m_player) {
        ijkmp_stop(m_player);
        m_msgTimer->stop();
    }
}

void IJKMediaPlayer::seekTo(qint64 msec) {
    if (m_player) {
        ijkmp_seek_to(m_player, msec);
    }
}

IJKMediaPlayer::State IJKMediaPlayer::state() const {
    return m_player ? (State)ijkmp_get_state(m_player) : Idle;
}

bool IJKMediaPlayer::isPlaying() const {
    return m_player ? ijkmp_is_playing(m_player) : false;
}

qint64 IJKMediaPlayer::currentPosition() const {
    return m_player ? ijkmp_get_current_position(m_player) : 0;
}

qint64 IJKMediaPlayer::duration() const {
    return m_player ? ijkmp_get_duration(m_player) : 0;
}

void IJKMediaPlayer::setOption(int category, const QString &name, const QString &value) {
    if (m_player) {
        ijkmp_set_option(m_player, category, name.toUtf8().constData(), value.toUtf8().constData());
    }
}

void IJKMediaPlayer::setOptionInt(int category, const QString &name, qint64 value) {
    if (m_player) {
        ijkmp_set_option_int(m_player, category, name.toUtf8().constData(), value);
    }
}

void IJKMediaPlayer::setVideoWidget(IJKVideoWidget *widget) {
    m_videoWidget = widget;
    // TODO: 将 widget 关联到播放器的视频输出
}

void IJKMediaPlayer::onMessageLoop() {
    if (!m_player) return;
    
    AVMessage msg;
    while (ijkmp_get_msg(m_player, &msg, 0) > 0) {
        switch (msg.what) {
        case FFP_MSG_PREPARED:
            emit prepared();
            break;
        case FFP_MSG_COMPLETED:
            emit completed();
            break;
        case FFP_MSG_ERROR:
            emit error(msg.arg1);
            break;
        case FFP_MSG_CURRENT_POSITION_UPDATE:
            emit positionChanged(msg.arg1);
            break;
        default:
            break;
        }
        msg_free_res(&msg);
    }
}

int IJKMediaPlayer::messageLoop(void *arg) {
    // 消息循环由 Qt 定时器处理
    return 0;
}
```

---

### Step 5: 编写 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(ijkplayer-qt VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia OpenGL)

# FFmpeg
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/3rdparty/ffmpeg")
include_directories(${FFMPEG_DIR}/include)
link_directories(${FFMPEG_DIR}/lib)

# pthread
set(PTHREAD_DIR "${CMAKE_SOURCE_DIR}/3rdparty/pthread-win32")
include_directories(${PTHREAD_DIR}/include)
link_directories(${PTHREAD_DIR}/lib)

# ijkmedia 核心源文件
file(GLOB_RECURSE IJKPLAYER_SOURCES
    "ijkmedia/ijkplayer/*.c"
    "ijkmedia/ijkplayer/*.cpp"
)

file(GLOB_RECURSE IJKSDL_SOURCES
    "ijkmedia/ijksdl/*.c"
    "ijkmedia/ijksdl/windows/*.cpp"
)

# Qt 封装层
file(GLOB_RECURSE QT_WRAPPER_SOURCES
    "qt-wrapper/*.cpp"
    "qt-wrapper/*.h"
)

# 创建库
add_library(ijkplayer-core STATIC
    ${IJKPLAYER_SOURCES}
    ${IJKSDL_SOURCES}
)

target_include_directories(ijkplayer-core PUBLIC
    ${CMAKE_SOURCE_DIR}/ijkmedia
    ${CMAKE_SOURCE_DIR}/ijkmedia/ijkplayer
    ${CMAKE_SOURCE_DIR}/ijkmedia/ijksdl
)

target_link_libraries(ijkplayer-core
    avcodec avformat avutil swscale swresample
    pthreadVC3
)

# Qt 播放器库
add_library(ijkplayer-qt STATIC
    ${QT_WRAPPER_SOURCES}
)

target_link_libraries(ijkplayer-qt
    ijkplayer-core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Multimedia
    Qt5::OpenGL
)

# 示例程序
add_subdirectory(examples/SimplePlayer)
```

---

### Step 6: 创建示例播放器

```cpp
// examples/SimplePlayer/MainWindow.h
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "../../qt-wrapper/IJKMediaPlayer.h"
#include "../../qt-wrapper/IJKVideoWidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
private slots:
    void on_btnOpen_clicked();
    void on_btnPlay_clicked();
    void on_btnPause_clicked();
    void on_btnStop_clicked();
    void on_sliderProgress_sliderMoved(int position);
    
    void onPlayerPrepared();
    void onPlayerPositionChanged(qint64 position);
    void onPlayerDurationChanged(qint64 duration);
    
private:
    Ui::MainWindow *ui;
    IJKMediaPlayer *m_player;
    IJKVideoWidget *m_videoWidget;
};

#endif
```

```cpp
// examples/SimplePlayer/MainWindow.cpp
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QFileDialog>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_player(new IJKMediaPlayer(this))
    , m_videoWidget(new IJKVideoWidget(this)) {
    
    ui->setupUi(this);
    
    // 设置视频显示区域
    ui->videoLayout->addWidget(m_videoWidget);
    m_player->setVideoWidget(m_videoWidget);
    
    // 连接信号
    connect(m_player, &IJKMediaPlayer::prepared, this, &MainWindow::onPlayerPrepared);
    connect(m_player, &IJKMediaPlayer::positionChanged, this, &MainWindow::onPlayerPositionChanged);
    connect(m_player, &IJKMediaPlayer::durationChanged, this, &MainWindow::onPlayerDurationChanged);
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::on_btnOpen_clicked() {
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open Video"),
        "",
        tr("Video Files (*.mp4 *.avi *.mkv *.flv);;All Files (*.*)")
    );
    
    if (!fileName.isEmpty()) {
        m_player->setDataSource(fileName);
        m_player->prepareAsync();
        ui->statusbar->showMessage("Loading: " + fileName);
    }
}

void MainWindow::on_btnPlay_clicked() {
    m_player->start();
    ui->statusbar->showMessage("Playing");
}

void MainWindow::on_btnPause_clicked() {
    m_player->pause();
    ui->statusbar->showMessage("Paused");
}

void MainWindow::on_btnStop_clicked() {
    m_player->stop();
    ui->statusbar->showMessage("Stopped");
}

void MainWindow::on_sliderProgress_sliderMoved(int position) {
    qint64 duration = m_player->duration();
    if (duration > 0) {
        qint64 msec = (position * duration) / ui->sliderProgress->maximum();
        m_player->seekTo(msec);
    }
}

void MainWindow::onPlayerPrepared() {
    ui->statusbar->showMessage("Ready");
    qint64 duration = m_player->duration();
    ui->sliderProgress->setRange(0, 1000);
    qDebug() << "Duration:" << duration << "ms";
}

void MainWindow::onPlayerPositionChanged(qint64 position) {
    qint64 duration = m_player->duration();
    if (duration > 0) {
        int value = (position * 1000) / duration;
        ui->sliderProgress->setValue(value);
    }
    
    // 更新时间显示
    int sec = position / 1000;
    int min = sec / 60;
    sec = sec % 60;
    ui->lblTime->setText(QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0')));
}

void MainWindow::onPlayerDurationChanged(qint64 duration) {
    int sec = duration / 1000;
    int min = sec / 60;
    sec = sec % 60;
    ui->lblDuration->setText(QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0')));
}
```

---

## 四、关键技术要点

### 1. 平台适配要点

#### Windows 特定处理
- **日志输出**: 将 Android 的 ALOGD 宏改为 qDebug() 或 OutputDebugString()
- **线程**: ijksdl_thread 可以基于 Qt 的 QThread 或 Windows API
- **互斥锁**: ijksdl_mutex 可以基于 Qt 的 QMutex 或 Windows CRITICAL_SECTION

#### 编译宏定义
```cpp
// 在 config.h 中定义
#define __WINDOWS__
#undef __ANDROID__
#undef __APPLE__

// 日志宏替换
#ifdef __WINDOWS__
#include <QDebug>
#define ALOGD qDebug
#define ALOGE qCritical
#define ALOGW qWarning
#define ALOGI qInfo
#define MPTRACE qDebug
#endif
```

### 2. FFmpeg 集成

```cpp
// 在 ijkplayer 初始化时注册 FFmpeg
void ijkmp_global_init() {
    av_register_all();
    avformat_network_init();
    // ... 其他初始化
}
```

### 3. 性能优化

- **硬件加速**: Windows 可以使用 DXVA2 或 D3D11VA
```cpp
// 设置硬件解码选项
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_PLAYER, "videotoolbox", 1);  // macOS
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_PLAYER, "mediacodec", 1);   // Android
// Windows: 需要在 FFmpeg 配置中启用 DXVA2
```

- **多线程解码**:
```cpp
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_CODEC, "threads", 4);
```

### 4. 音视频同步

ijkplayer 使用三种同步方式:
- **AV_SYNC_AUDIO_MASTER**: 以音频为基准（默认）
- **AV_SYNC_VIDEO_MASTER**: 以视频为基准
- **AV_SYNC_EXTERNAL_CLOCK**: 以外部时钟为基准

Qt 实现时需要注意音频延迟的处理。

---

## 五、常见问题和解决方案

### 问题1: 链接错误 - undefined reference
**原因**: FFmpeg 和 ijkplayer C 代码在 C++ 项目中链接
**解决**:
```cpp
extern "C" {
#include "ijkplayer/ijkplayer.h"
#include "ijksdl/ijksdl_vout.h"
}
```

### 问题2: 视频渲染黑屏
**原因**: OpenGL 纹理格式或 YUV 转 RGB 着色器问题
**解决**: 
- 检查 glTexImage2D 格式参数
- 验证 YUV 数据的 linesize 和实际宽度
- 使用 RenderDoc 调试 OpenGL 调用

### 问题3: 音频卡顿
**原因**: QAudioOutput 缓冲区设置不当
**解决**:
```cpp
m_audioOutput->setBufferSize(desired->samples * 2 * desired->channels);
```

### 问题4: 跨平台编译
**建议**:
- 使用 CMake 管理跨平台构建
- 条件编译处理平台差异
- FFmpeg 使用统一的预编译库或子模块

---

## 六、进阶功能

### 1. 添加硬件解码支持
```cpp
// Windows DXVA2 支持
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_PLAYER, "videotoolbox", 0);
ijkmp_set_option(mp, IJKMP_OPT_CATEGORY_PLAYER, "overlay-format", "dxva2_vld");
```

### 2. 添加字幕支持
```cpp
// ijkplayer 本身不支持字幕渲染，需要：
// 1. 使用 FFmpeg 解析字幕流
// 2. 在 Qt 层面用 QLabel 或自定义绘制叠加字幕
```

### 3. 添加网络流支持
```cpp
// HTTP/HTTPS
ijkmp_set_data_source(mp, "http://example.com/video.mp4");

// RTMP/RTSP
ijkmp_set_data_source(mp, "rtmp://example.com/live/stream");
ijkmp_set_data_source(mp, "rtsp://example.com:554/stream");
```

### 4. 添加滤镜支持
虽然原 ijkplayer 不支持 avfilter，但可以自行扩展：
```cpp
// 在 ff_ffplay.c 的视频处理流程中插入 libavfilter
```

---

## 七、编译和运行

```bash
# 1. 克隆项目
git clone https://github.com/yourusername/ijkplayer-qt.git
cd ijkplayer-qt

# 2. 下载依赖
# 手动下载 FFmpeg 和 pthread-win32 到 3rdparty/

# 3. CMake 构建
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
cmake --build . --config Release

# 4. 运行示例
cd examples/SimplePlayer/Release
SimplePlayer.exe
```

---

## 八、参考资源

- **ijkplayer 官方仓库**: https://github.com/bilibili/ijkplayer
- **FFmpeg 官方文档**: https://ffmpeg.org/documentation.html
- **Qt Multimedia**: https://doc.qt.io/qt-5/qtmultimedia-index.html
- **Qt OpenGL**: https://doc.qt.io/qt-5/qtopengl-index.html

---

## 总结

移植 ijkplayer 到 Qt Windows 的核心工作是：
1. **复用核心播放器逻辑**（90%以上代码无需改动）
2. **实现 Windows SDL 层**（音视频输出适配）
3. **创建 Qt 封装类**（提供 QObject 接口）
4. **编译 FFmpeg for Windows**

整个移植工作量主要在 SDL 层的实现和调试，预计需要 1-2 周时间完成基本功能。

移植后的优势：
- ✅ 跨平台（Windows, Linux, macOS）
- ✅ 成熟的播放器内核
- ✅ 支持多种格式和协议
- ✅ Qt 原生集成
- ✅ 易于扩展和定制


