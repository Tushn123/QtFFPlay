# ijkplayer 移植 Qt Windows - 快速参考

## 📊 一图看懂架构

```
原项目架构:
┌─────────────────────────────────────────────┐
│ Android/iOS App                             │
│  └─ Java/ObjC 播放器接口                    │
└─────────────────────────────────────────────┘
              ↕ JNI / ObjC Bridge
┌─────────────────────────────────────────────┐
│ ijkplayer API (ijkplayer.h/c)               │ ← 保留
│  - play/pause/seek/stop                     │
│  - 状态管理                                 │
└─────────────────────────────────────────────┘
              ↕
┌─────────────────────────────────────────────┐
│ ff_ffplay (ff_ffplay.c)                     │ ← 保留
│  - 解码线程                                 │
│  - 音视频同步                               │
│  - 缓冲控制                                 │
└─────────────────────────────────────────────┘
              ↕
┌─────────────────────────────────────────────┐
│ ijksdl (SDL抽象层)                          │
│  ├─ ijksdl_vout (视频输出)                  │ ← 保留接口
│  └─ ijksdl_aout (音频输出)                  │ ← 保留接口
└─────────────────────────────────────────────┘
              ↕
┌─────────────────────────────────────────────┐
│ android/ios 平台实现                        │ ← 替换为Qt实现
│  ├─ AudioTrack/AudioQueue                   │
│  └─ NativeWindow/OpenGL ES                  │
└─────────────────────────────────────────────┘


新架构 (Qt Windows):
┌─────────────────────────────────────────────┐
│ Qt 应用 (QWidget/QML)                       │ 🆕
│  └─ IJKMediaPlayer (Qt封装类)              │
└─────────────────────────────────────────────┘
              ↕ Qt Signal/Slot
┌─────────────────────────────────────────────┐
│ ijkplayer API (ijkplayer.h/c)               │ ✅ 保留
└─────────────────────────────────────────────┘
              ↕
┌─────────────────────────────────────────────┐
│ ff_ffplay (ff_ffplay.c)                     │ ✅ 保留
└─────────────────────────────────────────────┘
              ↕
┌─────────────────────────────────────────────┐
│ ijksdl (SDL抽象层)                          │ ✅ 保留
│  ├─ ijksdl_vout.h                           │
│  └─ ijksdl_aout.h                           │
└─────────────────────────────────────────────┘
              ↕
┌─────────────────────────────────────────────┐
│ Windows Qt 实现                             │ 🆕 新实现
│  ├─ QAudioOutput (音频)                     │
│  ├─ QOpenGLWidget (视频)                    │
│  └─ QThread/QMutex (线程)                   │
└─────────────────────────────────────────────┘
```

---

## 🗂️ 移植后的项目结构

```
ijkplayer-qt/
│
├── 3rdparty/                    # 第三方依赖
│   ├── ffmpeg/                  # FFmpeg Windows 库
│   │   ├── bin/                 # DLL文件
│   │   ├── include/             # 头文件
│   │   └── lib/                 # 导入库
│   └── pthread-win32/           # pthread Windows实现（可选）
│
├── ijkmedia/                    # 核心播放器（从原项目复制）
│   │
│   ├── ijkplayer/               ✅ 核心播放器逻辑（完全保留）
│   │   ├── ijkplayer.h/c        - 播放器API
│   │   ├── ff_ffplay.h/c        - 核心播放逻辑 ⭐
│   │   ├── ff_ffplay_def.h      - 核心结构定义
│   │   ├── ff_ffpipeline.h/c    - 解码管道
│   │   ├── ijkavformat/         - 自定义IO
│   │   ├── ijkavutil/           - 工具函数
│   │   └── pipeline/            - 管道实现
│   │
│   └── ijksdl/                  ✅ SDL层（部分保留+新增）
│       ├── ijksdl_vout.h/c      ✅ 视频输出接口（保留）
│       ├── ijksdl_aout.h/c      ✅ 音频输出接口（保留）
│       ├── ijksdl_thread.h/c    ✅ 线程封装（保留/修改）
│       ├── ijksdl_mutex.h/c     ✅ 互斥锁（保留/修改）
│       ├── ijksdl_timer.h/c     ✅ 定时器（保留）
│       ├── ijksdl_*.h           ✅ 其他基础封装（保留）
│       │
│       ├── windows/             🆕 Windows平台实现（新建）
│       │   ├── ijksdl_vout_qt_opengl.h/cpp
│       │   ├── ijksdl_aout_qt_audio.h/cpp
│       │   └── ijksdl_inc_internal_windows.h
│       │
│       ├── ffmpeg/              ✅ FFmpeg相关（保留）
│       │   └── ijksdl_vout_overlay_ffmpeg.c
│       │
│       └── gles2/               ✅ OpenGL渲染（可选保留）
│           ├── renderer_yuv420p.c
│           └── shader.c
│
├── qt-wrapper/                  🆕 Qt C++封装层（新建）
│   ├── IJKMediaPlayer.h/cpp     - Qt播放器类
│   ├── IJKVideoWidget.h/cpp     - 视频显示控件
│   └── IJKAudioOutput.h/cpp     - 音频输出封装
│
├── examples/                    🆕 示例程序
│   └── SimplePlayer/
│       ├── main.cpp
│       ├── MainWindow.h/cpp/ui
│       └── SimplePlayer.pro
│
├── CMakeLists.txt               🆕 CMake构建脚本
├── README.md
└── LICENSE
```

---

## 📝 核心文件对照表

| 原文件 (Android/iOS) | 新文件 (Qt Windows) | 状态 | 说明 |
|---------------------|-------------------|------|------|
| `ijkplayer.h/c` | 保持不变 | ✅ 保留 | 播放器主API |
| `ff_ffplay.h/c` | 保持不变 | ✅ 保留 | 核心播放逻辑 |
| `ijksdl_vout.h` | 保持不变 | ✅ 保留 | 视频输出接口定义 |
| `ijksdl_aout.h` | 保持不变 | ✅ 保留 | 音频输出接口定义 |
| `android/ijksdl_vout_android_nativewindow.c` | `windows/ijksdl_vout_qt_opengl.cpp` | 🆕 新建 | 视频渲染实现 |
| `android/ijksdl_aout_android_audiotrack.c` | `windows/ijksdl_aout_qt_audio.cpp` | 🆕 新建 | 音频输出实现 |
| `android/ijkplayer_jni.c` | `qt-wrapper/IJKMediaPlayer.cpp` | 🆕 新建 | 平台绑定层 |
| `android/ijkplayer-java/` | `qt-wrapper/IJKMediaPlayer.h` | 🆕 新建 | Qt C++接口 |

---

## 🔧 关键代码片段

### 1. 创建播放器

```cpp
// Qt 封装
class IJKMediaPlayer : public QObject {
    Q_OBJECT
public:
    IJKMediaPlayer(QObject *parent = nullptr) {
        ijkmp_global_init();
        m_player = ijkmp_create(messageLoop);
        ijkmp_set_weak_thiz(m_player, this);
    }
    
    void setDataSource(const QString &url) {
        ijkmp_set_data_source(m_player, url.toUtf8().data());
    }
    
    void prepareAsync() {
        ijkmp_prepare_async(m_player);
    }
    
    void start() {
        ijkmp_start(m_player);
    }
    
private:
    IjkMediaPlayer *m_player;
};
```

### 2. 视频渲染（Qt OpenGL）

```cpp
class IJKVideoWidget : public QOpenGLWidget {
public:
    void displayYUVFrame(uint8_t *y, uint8_t *u, uint8_t *v,
                         int w, int h) {
        // 更新纹理
        glBindTexture(GL_TEXTURE_2D, m_texY);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, 
                     GL_RED, GL_UNSIGNED_BYTE, y);
        
        glBindTexture(GL_TEXTURE_2D, m_texU);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w/2, h/2, 0, 
                     GL_RED, GL_UNSIGNED_BYTE, u);
        
        glBindTexture(GL_TEXTURE_2D, m_texV);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w/2, h/2, 0, 
                     GL_RED, GL_UNSIGNED_BYTE, v);
        
        update(); // 触发重绘
    }
    
protected:
    void paintGL() override {
        // YUV → RGB 着色器渲染
        m_program->bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
};
```

### 3. 音频输出（Qt Audio）

```cpp
class IJKAudioOutput {
public:
    int open(const SDL_AudioSpec *desired) {
        QAudioFormat format;
        format.setSampleRate(desired->freq);
        format.setChannelCount(desired->channels);
        format.setSampleSize(16);
        format.setCodec("audio/pcm");
        
        m_audioOutput = new QAudioOutput(format);
        m_audioDevice = m_audioOutput->start();
        return 0;
    }
    
    int write(uint8_t *data, int len) {
        return m_audioDevice->write((char*)data, len);
    }
    
private:
    QAudioOutput *m_audioOutput;
    QIODevice *m_audioDevice;
};
```

### 4. C 接口封装

```cpp
// 将 C++ 实现包装为 C 接口
extern "C" {
    SDL_Vout* SDL_VoutQt_Create(void* widget) {
        SDL_Vout *vout = (SDL_Vout*)calloc(1, sizeof(SDL_Vout));
        SDL_Vout_Opaque *opaque = (SDL_Vout_Opaque*)calloc(1, sizeof(SDL_Vout_Opaque));
        
        opaque->widget = new IJKVideoWidget();
        vout->opaque = opaque;
        vout->display_overlay = func_display_overlay;
        vout->free_l = func_free_l;
        
        return vout;
    }
}
```

---

## ⚙️ 编译配置

### CMakeLists.txt 核心部分

```cmake
# Qt
find_package(Qt5 REQUIRED COMPONENTS 
    Core Gui Widgets Multimedia OpenGL)

# FFmpeg
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/3rdparty/ffmpeg")
include_directories(${FFMPEG_DIR}/include)
link_directories(${FFMPEG_DIR}/lib)

# ijkplayer 核心库
add_library(ijkplayer-core STATIC
    ${IJKPLAYER_SOURCES}
    ${IJKSDL_SOURCES}
)

target_link_libraries(ijkplayer-core
    avcodec avformat avutil swscale swresample
)

# Qt 封装库
add_library(ijkplayer-qt STATIC
    ${QT_WRAPPER_SOURCES}
)

target_link_libraries(ijkplayer-qt
    ijkplayer-core
    Qt5::Core Qt5::Widgets Qt5::Multimedia Qt5::OpenGL
)
```

### .pro 文件（qmake）

```qmake
QT += core gui widgets multimedia opengl

CONFIG += c++11

INCLUDEPATH += $$PWD/../../ijkmedia \
               $$PWD/../../3rdparty/ffmpeg/include

LIBS += -L$$PWD/../../build/ijkmedia -lijkplayer-core \
        -L$$PWD/../../3rdparty/ffmpeg/lib \
        -lavcodec -lavformat -lavutil -lswscale -lswresample

SOURCES += main.cpp MainWindow.cpp

HEADERS += MainWindow.h

FORMS += MainWindow.ui
```

---

## 🚀 快速开始步骤

### 1️⃣ 准备环境（1天）
```bash
# 安装工具
- Visual Studio 2019+
- Qt 5.15+ / Qt 6.x
- CMake 3.16+

# 下载 FFmpeg
https://github.com/BtbN/FFmpeg-Builds/releases
# 解压到 3rdparty/ffmpeg/
```

### 2️⃣ 复制核心代码（半天）
```bash
# 从原 ijkplayer 项目复制
cp -r ijkplayer-master/ijkmedia/ijkplayer ijkplayer-qt/ijkmedia/
cp -r ijkplayer-master/ijkmedia/ijksdl ijkplayer-qt/ijkmedia/

# 删除不需要的 Android/iOS 代码
rm -rf ijkplayer-qt/ijkmedia/ijksdl/android
rm -rf ijkplayer-qt/ijkmedia/ijkplayer/android
```

### 3️⃣ 实现 SDL 层（3-5天）
```bash
# 新建文件
mkdir ijkplayer-qt/ijkmedia/ijksdl/windows

# 创建实现文件
touch ijkplayer-qt/ijkmedia/ijksdl/windows/ijksdl_vout_qt_opengl.h
touch ijkplayer-qt/ijkmedia/ijksdl/windows/ijksdl_vout_qt_opengl.cpp
touch ijkplayer-qt/ijkmedia/ijksdl/windows/ijksdl_aout_qt_audio.h
touch ijkplayer-qt/ijkmedia/ijksdl/windows/ijksdl_aout_qt_audio.cpp

# 实现视频和音频输出（参考详细文档）
```

### 4️⃣ 创建 Qt 封装（2-3天）
```bash
mkdir qt-wrapper
cd qt-wrapper

# 创建 Qt 封装类
touch IJKMediaPlayer.h IJKMediaPlayer.cpp
touch IJKVideoWidget.h IJKVideoWidget.cpp

# 实现 QObject 接口和信号槽
```

### 5️⃣ 编译和测试（1-2天）
```bash
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" \
         -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
cmake --build . --config Release

# 运行示例
cd examples/SimplePlayer/Release
SimplePlayer.exe
```

### 6️⃣ 调试优化（5-7天）
- 音视频同步调试
- 内存泄漏检查
- 性能优化
- 多格式测试

---

## 🎯 核心 API 使用

### 基本播放流程

```cpp
// 1. 初始化
ijkmp_global_init();

// 2. 创建播放器
IjkMediaPlayer *mp = ijkmp_create(msg_loop);

// 3. 设置选项
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_PLAYER, 
                     "start-on-prepared", 0);

// 4. 设置数据源
ijkmp_set_data_source(mp, "video.mp4");

// 5. 准备（异步）
ijkmp_prepare_async(mp);

// 6. 等待 FFP_MSG_PREPARED 消息
// （在消息循环中处理）

// 7. 开始播放
ijkmp_start(mp);

// 8. 控制播放
ijkmp_pause(mp);          // 暂停
ijkmp_start(mp);          // 恢复
ijkmp_seek_to(mp, 10000); // 跳转到 10s
ijkmp_stop(mp);           // 停止

// 9. 清理
ijkmp_shutdown(mp);
ijkmp_dec_ref_p(&mp);
ijkmp_global_uninit();
```

### 消息处理

```cpp
// 消息循环
AVMessage msg;
while (ijkmp_get_msg(mp, &msg, 1) > 0) {
    switch (msg.what) {
    case FFP_MSG_PREPARED:
        // 准备完成
        qDebug() << "Player prepared";
        break;
        
    case FFP_MSG_COMPLETED:
        // 播放完成
        qDebug() << "Playback completed";
        break;
        
    case FFP_MSG_VIDEO_SIZE_CHANGED:
        // 视频尺寸变化
        qDebug() << "Video size:" << msg.arg1 << "x" << msg.arg2;
        break;
        
    case FFP_MSG_ERROR:
        // 错误
        qDebug() << "Error:" << msg.arg1;
        break;
        
    case FFP_MSG_BUFFERING_START:
        // 开始缓冲
        qDebug() << "Buffering...";
        break;
        
    case FFP_MSG_BUFFERING_END:
        // 缓冲结束
        qDebug() << "Buffering end";
        break;
    }
    
    msg_free_res(&msg);
}
```

### 常用选项

```cpp
// 播放器选项
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_PLAYER, 
                     "start-on-prepared", 0);  // 准备后不自动播放
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_PLAYER, 
                     "enable-accurate-seek", 1); // 精确seek

// 格式选项
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_FORMAT, 
                     "analyzeduration", 200000); // 分析时长(微秒)
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_FORMAT, 
                     "probesize", 10240);        // 探测大小

// 编解码选项
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_CODEC, 
                     "skip_loop_filter", 48);    // 跳过循环滤波
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_CODEC, 
                     "threads", 4);              // 解码线程数

// 网络选项
ijkmp_set_option(mp, IJKMP_OPT_CATEGORY_FORMAT, 
                 "user-agent", "MyPlayer/1.0"); // User-Agent
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_FORMAT, 
                     "timeout", 10000000);       // 超时(微秒)
```

---

## 📊 性能优化建议

| 项目 | 建议 | 说明 |
|------|------|------|
| **解码线程** | 4-8 | `threads` 选项 |
| **音频缓冲** | 1024-2048 | 平衡延迟和流畅度 |
| **视频缓冲** | 3-5 帧 | `max_fps` 选项 |
| **OpenGL** | 使用 VBO | 减少 CPU→GPU 传输 |
| **纹理格式** | GL_RED | 避免 GL_LUMINANCE（已废弃） |
| **YUV转换** | GPU着色器 | 避免 CPU swscale |

---

## 🐛 常见问题速查

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| 链接错误 | C/C++混编 | 使用 `extern "C"` |
| 黑屏 | OpenGL纹理格式错误 | 检查 YUV→RGB 着色器 |
| 音频卡顿 | 缓冲区太小 | 增大 `QAudioOutput` 缓冲 |
| 内存泄漏 | 未释放资源 | 确保调用 `msg_free_res()` |
| 音视频不同步 | 时钟漂移 | 检查音频输出延迟 |
| 编译失败 | 找不到头文件 | 检查 `INCLUDEPATH` |

---

## 📚 进阶功能

### 硬件解码（Windows）
```cpp
// DXVA2 硬件解码
ijkmp_set_option_int(mp, IJKMP_OPT_CATEGORY_PLAYER, 
                     "mediacodec", 1);
// 需要 FFmpeg 编译时启用 --enable-dxva2
```

### 网络流播放
```cpp
// HTTP
ijkmp_set_data_source(mp, "http://example.com/video.mp4");

// HLS
ijkmp_set_data_source(mp, "http://example.com/stream.m3u8");

// RTMP
ijkmp_set_data_source(mp, "rtmp://example.com/live/stream");
```

### 倍速播放
```cpp
ijkmp_set_playback_rate(mp, 1.5f);  // 1.5倍速
ijkmp_set_playback_rate(mp, 0.5f);  // 0.5倍速
```

### 音量控制
```cpp
ijkmp_set_playback_volume(mp, 0.5f); // 50%音量
```

---

## ✅ 检查清单

移植完成前的检查项：

- [ ] FFmpeg 库正确链接
- [ ] 视频能正常显示
- [ ] 音频能正常播放
- [ ] 音视频同步正常
- [ ] 播放控制功能正常（play/pause/seek/stop）
- [ ] 进度条显示正确
- [ ] 支持多种格式（mp4/avi/mkv/flv）
- [ ] 网络流能播放（http/rtsp/rtmp）
- [ ] 内存无泄漏（Valgrind/DrMemory 检查）
- [ ] 多次播放无崩溃
- [ ] CPU 占用合理（< 20%）
- [ ] 跨 Windows 版本测试（Win7/10/11）

---

## 🔗 有用的链接

- **ijkplayer 官方仓库**: https://github.com/bilibili/ijkplayer
- **FFmpeg 下载**: https://github.com/BtbN/FFmpeg-Builds/releases
- **Qt 下载**: https://download.qt.io/
- **示例项目**: 参考 `ijkplayer_to_qt_windows_migration_guide.md`

---

**总结**: ijkplayer 移植到 Qt Windows 是可行的，核心工作是实现 SDL 层的 Windows 版本。预计 3-4 周完成基本功能。


