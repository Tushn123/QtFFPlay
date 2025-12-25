/*
 * Copyright (c) 2024 FFPlayer contributors
 *
 * PlayerWidget 实现 - 使用 MediaPlayer 层
 * 支持两种渲染模式：SDL 子窗口渲染 和 Qt OpenGL 渲染
 */

#include "PlayerWidget.h"
#include "VideoGLWidget.h"
#include "ThumbnailExtractor.h"
#include <QKeyEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QDebug>
#include <QPainter>
#include <QPalette>
#include <QMetaObject>

// 避免 C 头文件中的 class 关键字冲突
#define class class_name
extern "C" {
#include "player/mediaplayer.h"
#include "player/ff_ffmsg.h"
#include "player/ff_ffmsg_queue.h"
}
#undef class

/*
 * =============================================================================
 * 辅助函数
 * =============================================================================
 */

// 检测是否为直播流 URL（用于跳过不适用的功能，如缩略图提取）
bool PlayerWidget::isLiveUrl(const QString &url)
{
    if (url.isEmpty())
        return false;
    
    // 基于 URL 协议检测
    if (url.startsWith("rtmp://", Qt::CaseInsensitive) ||
        url.startsWith("rtmps://", Qt::CaseInsensitive) ||
        url.startsWith("rtmpt://", Qt::CaseInsensitive) ||
        url.startsWith("rtsp://", Qt::CaseInsensitive) ||
        url.startsWith("rtsps://", Qt::CaseInsensitive) ||
        url.startsWith("rtp://", Qt::CaseInsensitive) ||
        url.startsWith("udp://", Qt::CaseInsensitive) ||
        url.startsWith("srt://", Qt::CaseInsensitive)) {
        return true;
    }
    
    // HTTP-FLV 直播流检测
    if (url.contains(".flv", Qt::CaseInsensitive) &&
        (url.startsWith("http://", Qt::CaseInsensitive) || 
         url.startsWith("https://", Qt::CaseInsensitive))) {
        return true;
    }
    
    // HLS 直播流检测
    if (url.contains(".m3u8", Qt::CaseInsensitive) &&
        (url.startsWith("http://", Qt::CaseInsensitive) || 
         url.startsWith("https://", Qt::CaseInsensitive))) {
        return true;
    }
    
    return false;
}

/*
 * =============================================================================
 * 硬件加速配置（唯一入口）
 * =============================================================================
 * PlayerWidget 是硬件加速类型的唯一配置入口。
 * 底层 FFPlayer/MediaPlayer 只提供接口，不设置默认值。
 * 
 * 修改下方 m_hwAccelType 的值来切换解码方式：
 *   - HWAccelType::None         软解码（CPU 解码）
 *   - HWAccelType::D3D11VA      Windows 硬解码（推荐）
 *   - HWAccelType::VideoToolbox macOS 硬解码（推荐）
 *   - HWAccelType::VAAPI        Linux 硬解码（推荐）
 *   - HWAccelType::Auto         自动选择
 */
PlayerWidget::PlayerWidget(QWidget *parent)
    : QWidget(parent)
    , m_mp(nullptr)
    , m_initialized(false)
    , m_renderMode(RenderMode::OpenGL)
#if defined(_WIN32)
    , m_hwAccelType(HWAccelType::D3D11VA)  // ← Windows: 修改此处切换解码方式
#elif defined(__APPLE__)
    , m_hwAccelType(HWAccelType::VideoToolbox)  // ← macOS: 修改此处切换解码方式
#elif defined(__linux__)
    , m_hwAccelType(HWAccelType::VAAPI)  // ← Linux: 修改此处切换解码方式
#else
    , m_hwAccelType(HWAccelType::Auto)
#endif
    , m_videoWidget(nullptr)
    , m_layout(nullptr)
    , m_thumbnailExtractor(nullptr)
    , m_msgLoopRunning(false)
    , m_lastState(MP_STATE_IDLE)
    , m_startOnPrepared(false)
    , m_loopCount(0)           // 默认不循环
    , m_currentLoopIndex(0)
    , m_positionTimer(nullptr)
    , m_lastPosition(0)
    , m_spacePressed(false)
    , m_isPanning(false)
    , m_lastMousePos(0, 0)
    , m_mediaType(MediaType::Unknown)
    , m_isSeekable(true)
{
    // 设置焦点策略以接收键盘事件
    setFocusPolicy(Qt::StrongFocus);
    
    // 启用鼠标追踪
    setMouseTracking(true);
    
    // 设置最小尺寸
    setMinimumSize(320, 240);
    
    // 设置深灰色背景（与 VideoWidget 一致）
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(19, 19, 19));
    setPalette(pal);
    
    // 设置布局
    setupLayout();
    
    // 创建缩略图提取器（用于任意位置预览）
    m_thumbnailExtractor = new ThumbnailExtractor(this);
    connect(m_thumbnailExtractor, &ThumbnailExtractor::thumbnailReady,
            this, [this](const QImage &image, qint64 positionMs) {
        Q_UNUSED(positionMs);
        emit previewFrameReady(image);
    });
    
    // 创建播放位置更新定时器（每 200ms 更新一次）
    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(200);
    connect(m_positionTimer, &QTimer::timeout, this, &PlayerWidget::updatePosition);
    
    // 连接消息处理信号槽（使用 QueuedConnection 跨线程）
    connect(this, &PlayerWidget::stateChanged, this, [this](int state) {
        qDebug() << "[PlayerWidget] State changed to:" << state;
        
        // 根据状态控制位置更新定时器
        if (state == MP_STATE_STARTED) {
            // 播放状态，确保定时器运行
            if (m_positionTimer && !m_positionTimer->isActive()) {
                m_positionTimer->start();
            }
        } else if (state == MP_STATE_PAUSED) {
            // 暂停状态，保持定时器运行以便 seek 时能更新位置
            // 但更新一次当前位置
            updatePosition();
        } else if (state == MP_STATE_COMPLETED || 
                   state == MP_STATE_STOPPED || state == MP_STATE_ERROR ||
                   state == MP_STATE_END) {
            // 停止/完成/错误时，停止定时器
            if (m_positionTimer) {
                m_positionTimer->stop();
            }
            updatePosition();
        }
    });
}

void PlayerWidget::setupLayout()
{
    // 创建布局
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    
    // 创建 OpenGL 渲染组件
    m_videoWidget = new VideoGLWidget(this);
    m_layout->addWidget(m_videoWidget);
    
    // 连接视频尺寸变化信号
    connect(m_videoWidget, &VideoGLWidget::videoSizeChanged, this, &PlayerWidget::videoSizeChanged);
}

void PlayerWidget::setRenderMode(RenderMode mode)
{
    if (m_initialized) {
        qWarning() << "[PlayerWidget] Cannot change render mode after initialization";
        return;
    }
    m_renderMode = mode;
    
    // 根据模式显示/隐藏 OpenGL 组件
    if (m_videoWidget) {
        m_videoWidget->setVisible(mode == RenderMode::OpenGL);
    }
    
    // SDL 模式需要原生窗口句柄
    if (mode == RenderMode::SDL) {
        setAttribute(Qt::WA_NativeWindow);
    }
    
    qDebug() << "[PlayerWidget] Render mode set to:" << (mode == RenderMode::OpenGL ? "OpenGL" : "SDL");
}

PlayerWidget::~PlayerWidget()
{
    cleanupPlayer();
}

void PlayerWidget::initPlayer()
{
    if (m_initialized)
        return;
    
    // 全局初始化
    mp_global_init();
    
    // 创建 MediaPlayer 实例
    m_mp = mp_create();
    if (!m_mp) {
        qWarning() << "Failed to create MediaPlayer instance";
        emit errorOccurred("Failed to create MediaPlayer instance");
        return;
    }
    
    // 设置渲染模式
    if (m_renderMode == RenderMode::OpenGL) {
        mp_set_render_mode(m_mp, FFP_RENDER_MODE_CALLBACK);
        mp_set_video_frame_callback(m_mp, videoFrameCallback, this);
        qDebug() << "[PlayerWidget] Using OpenGL callback rendering";
    } else {
        mp_set_render_mode(m_mp, FFP_RENDER_MODE_SDL);
        qDebug() << "[PlayerWidget] Using SDL rendering";
    }
    
    // 设置硬件加速类型
    mp_set_hwaccel_type(m_mp, static_cast<MPHWAccelType>(static_cast<int>(m_hwAccelType)));
    qDebug() << "[PlayerWidget] HWAccel type:" << hwAccelName(m_hwAccelType);
    
    m_initialized = true;
    m_lastState = MP_STATE_IDLE;
    qDebug() << "MediaPlayer initialized, state:" << mp_get_state(m_mp);
}

// 视频帧回调函数（静态）
void PlayerWidget::videoFrameCallback(void *opaque, FFPVideoFrame *frame)
{
    PlayerWidget *self = static_cast<PlayerWidget*>(opaque);
    if (self && self->m_videoWidget) {
        self->m_videoWidget->updateFrame(frame);
    }
}

void PlayerWidget::cleanupPlayer()
{
    qDebug() << "[PlayerWidget] cleanupPlayer called";
    
    // 停止位置更新定时器
    if (m_positionTimer) {
        m_positionTimer->stop();
    }
    
    // 停止消息循环线程
    stopMessageLoop();
    
    if (m_mp) {
        // 先关闭 MediaPlayer（会停止所有线程）
        qDebug() << "[PlayerWidget] Shutting down MediaPlayer...";
        mp_shutdown(m_mp);
        
        // 再释放资源
        qDebug() << "[PlayerWidget] Releasing MediaPlayer...";
        mp_release(m_mp);
        m_mp = nullptr;
    }
    
    // 清除视频显示
    if (m_videoWidget) {
        m_videoWidget->clearFrame();
    }
    
    mp_global_uninit();
    m_initialized = false;
    qDebug() << "[PlayerWidget] cleanupPlayer done";
}

void PlayerWidget::setMedia(const QString &path)
{
    m_mediaPath = path;
    qDebug() << "setMedia:" << path;
    
    // 重置自动播放标志和循环计数
    m_startOnPrepared = false;
    m_currentLoopIndex = 0;
    
    // 清除之前的视频帧
    if (m_videoWidget) {
        m_videoWidget->clearFrame();
    }
    
    // 如果已有播放器，完全销毁并重建（确保所有状态干净）
    if (m_mp) {
        qDebug() << "[PlayerWidget] Destroying existing MediaPlayer for clean restart";
        
        // 停止消息循环
        stopMessageLoop();
        
        // 销毁旧的 MediaPlayer
        mp_shutdown(m_mp);
        mp_dec_ref_p(&m_mp);
        m_mp = nullptr;
        m_initialized = false;
    }
    
    // 重新初始化播放器
    initPlayer();
    
    if (!m_mp) {
        qWarning() << "MediaPlayer not initialized";
        return;
    }
    
    // SDL 模式需要附加窗口
    if (m_renderMode == RenderMode::SDL) {
        if (mp_attach_window(m_mp, (void*)winId(), width(), height()) < 0) {
            qWarning() << "Failed to attach window";
            emit errorOccurred("Failed to attach window");
            return;
        }
        qDebug() << "Window attached successfully (SDL mode)";
    } else {
        qDebug() << "Using OpenGL callback mode, no window attachment needed";
    }
    
    // 设置数据源
    if (mp_set_data_source(m_mp, m_mediaPath.toUtf8().constData()) < 0) {
        qWarning() << "Failed to set data source:" << m_mediaPath;
        emit errorOccurred("Failed to set data source: " + m_mediaPath);
        return;
    }
    
    qDebug() << "Data source set, state:" << mp_get_state(m_mp);
    
    // 打开缩略图提取器（用于进度条预览）
    // 注意：直播流不支持预览（不能 seek），跳过 ThumbnailExtractor
    // 这也避免了在 UI 线程中同步打开网络流导致界面卡死
    if (m_thumbnailExtractor && !isLiveUrl(m_mediaPath)) {
        // 转换 PlayerWidget 的硬件加速类型到 ThumbnailExtractor 的类型
        ThumbnailHWAccelType thumbHWType = ThumbnailHWAccelType::None;
        switch (m_hwAccelType) {
        case HWAccelType::Auto: thumbHWType = ThumbnailHWAccelType::Auto; break;
        case HWAccelType::DXVA2: thumbHWType = ThumbnailHWAccelType::DXVA2; break;
        case HWAccelType::D3D11VA: thumbHWType = ThumbnailHWAccelType::D3D11VA; break;
        case HWAccelType::CUDA: thumbHWType = ThumbnailHWAccelType::CUDA; break;
        case HWAccelType::VAAPI: thumbHWType = ThumbnailHWAccelType::VAAPI; break;
        case HWAccelType::VDPAU: thumbHWType = ThumbnailHWAccelType::VDPAU; break;
        case HWAccelType::VideoToolbox: thumbHWType = ThumbnailHWAccelType::VideoToolbox; break;
        case HWAccelType::QSV: thumbHWType = ThumbnailHWAccelType::QSV; break;
        default: thumbHWType = ThumbnailHWAccelType::None; break;
        }
        m_thumbnailExtractor->setHWAccelType(thumbHWType);
        m_thumbnailExtractor->open(m_mediaPath);
    } else if (isLiveUrl(m_mediaPath)) {
        qDebug() << "[PlayerWidget] Live stream detected, skipping ThumbnailExtractor";
    }
    
    // 启动消息循环线程（ijkplayer 风格：上层驱动消息循环）
    startMessageLoop();
    
    // 异步准备
    if (mp_prepare_async(m_mp) < 0) {
        qWarning() << "Failed to prepare media:" << m_mediaPath;
        emit errorOccurred("Failed to prepare media: " + m_mediaPath);
        return;
    }
    
    qDebug() << "Media preparing, state:" << mp_get_state(m_mp);
}

void PlayerWidget::play()
{
    qDebug() << "[PlayerWidget] play() called, m_mp=" << m_mp;
    if (!m_mp) {
        qDebug() << "[PlayerWidget] play() - no media player";
        return;
    }
    
    int state = mp_get_state(m_mp);
    qDebug() << "[PlayerWidget] play() - current state:" << state;
    
    // 如果还在准备中，设置标志，等准备完成后自动播放
    if (state == MP_STATE_ASYNC_PREPARING) {
        qDebug() << "[PlayerWidget] Still preparing (state=2), will start on prepared";
        m_startOnPrepared = true;
        return;
    }
    
    // 如果状态是 ERROR，不要尝试播放
    if (state == MP_STATE_ERROR) {
        qWarning() << "[PlayerWidget] Cannot play in ERROR state (state=8)";
        return;
    }
    
    // 其他状态，直接调用 mp_start
    qDebug() << "[PlayerWidget] Calling mp_start, state before:" << state;
    int ret = mp_start(m_mp);
    int stateAfter = mp_get_state(m_mp);
    qDebug() << "[PlayerWidget] mp_start returned:" << ret << ", state before:" << state << ", state after:" << stateAfter;
    
    // 确保定时器启动
    if (ret >= 0 && m_positionTimer && !m_positionTimer->isActive()) {
        m_positionTimer->start();
    }
}

void PlayerWidget::pause()
{
    if (m_mp) {
        int ret = mp_pause(m_mp);
        qDebug() << "mp_pause returned:" << ret << ", state:" << mp_get_state(m_mp);
    }
}

void PlayerWidget::stop()
{
    qDebug() << "[PlayerWidget] stop() called - full cleanup, this=" << this << "m_mp=" << m_mp;
    
    // 停止位置更新定时器
    if (m_positionTimer) {
        m_positionTimer->stop();
    }
    
    // 停止消息循环
    stopMessageLoop();
    
    // 销毁 MediaPlayer，释放所有资源
    if (m_mp) {
        qDebug() << "[PlayerWidget] Calling mp_shutdown for m_mp=" << m_mp;
        mp_shutdown(m_mp);
        qDebug() << "[PlayerWidget] Calling mp_dec_ref_p for m_mp=" << m_mp;
        mp_dec_ref_p(&m_mp);
        m_mp = nullptr;
        qDebug() << "[PlayerWidget] MediaPlayer destroyed";
    }
    
    // 关闭缩略图提取器
    if (m_thumbnailExtractor) {
        m_thumbnailExtractor->close();
    }
    
    // 清除视频显示
    if (m_videoWidget) {
        m_videoWidget->clearFrame();
    }
    
    // 重置初始化标志（下次 setMedia 会重新初始化）
    m_initialized = false;
    m_mediaPath.clear();
    
    qDebug() << "[PlayerWidget] stop() complete - all resources released";
}

void PlayerWidget::togglePause()
{
    if (m_mp) {
        // 根据当前状态决定播放或暂停
        int state = mp_get_state(m_mp);
        if (state == MP_STATE_PAUSED || state == MP_STATE_PREPARED || state == MP_STATE_COMPLETED) {
            play();
        } else if (state == MP_STATE_STARTED) {
            pause();
        }
    }
}

void PlayerWidget::stepForward()
{
    if (m_mp) {
        mp_step_to_next_frame(m_mp);
        qDebug() << "[PlayerWidget] Step forward";
    }
}

void PlayerWidget::seekTo(long msec)
{
    if (!m_mp) {
        return;
    }
    
    // 确保位置在有效范围内
    long duration = mp_get_duration(m_mp);
    if (duration > 0) {
        msec = qBound(0L, msec, duration);
    }
    
    qDebug() << "[PlayerWidget] Seeking to:" << msec << "ms";
    
    // 重置上次位置，确保 seek 后会发送位置更新
    m_lastPosition = -1;
    
    int ret = mp_seek_to(m_mp, msec);
    if (ret < 0) {
        qWarning() << "[PlayerWidget] Seek failed:" << ret;
    } else {
        // seek 成功后，确保定时器正在运行（如果是播放/暂停状态）
        int state = mp_get_state(m_mp);
        if ((state == MP_STATE_STARTED || state == MP_STATE_PAUSED) && 
            m_positionTimer && !m_positionTimer->isActive()) {
            m_positionTimer->start();
        }
        // 立即更新一次位置
        QTimer::singleShot(50, this, &PlayerWidget::updatePosition);
    }
}

void PlayerWidget::updatePosition()
{
    if (!m_mp) {
        return;
    }
    
    int state = mp_get_state(m_mp);
    
    // 非播放/暂停状态时不更新（但不阻止定时器）
    if (state != MP_STATE_STARTED && state != MP_STATE_PAUSED) {
        // 如果在播放状态下突然变成其他状态，保持定时器运行一段时间
        // 以便状态恢复后能继续更新
        return;
    }
    
    long position = mp_get_current_position(m_mp);
    
    // 位置变化或者强制更新（m_lastPosition == -1）时发送信号
    if (position != m_lastPosition || m_lastPosition < 0) {
        m_lastPosition = position;
        emit positionChanged(position);
    }
}

bool PlayerWidget::isPlaying() const
{
    if (m_mp) {
        return mp_is_playing(m_mp);
    }
    return false;
}

bool PlayerWidget::isPaused() const
{
    if (m_mp) {
        return mp_get_state(m_mp) == MP_STATE_PAUSED;
    }
    return true;
}

int PlayerWidget::getState() const
{
    if (m_mp) {
        return mp_get_state(m_mp);
    }
    return MP_STATE_END;
}

long PlayerWidget::getCurrentPosition() const
{
    if (m_mp) {
        return mp_get_current_position(m_mp);
    }
    return 0;
}

long PlayerWidget::getDuration() const
{
    if (m_mp) {
        return mp_get_duration(m_mp);
    }
    return 0;
}

void PlayerWidget::setPlaybackRate(float rate)
{
    if (m_mp) {
        mp_set_playback_rate(m_mp, rate);
        qDebug() << "[PlayerWidget] Playback rate set to:" << rate;
    }
}

float PlayerWidget::playbackRate() const
{
    if (m_mp) {
        return mp_get_playback_rate(m_mp);
    }
    return 1.0f;
}

void PlayerWidget::setVolume(int volume)
{
    if (m_mp) {
        // 转换 0-100 到 0.0-1.0
        float vol = qBound(0, volume, 100) / 100.0f;
        mp_set_volume(m_mp, vol);
        qDebug() << "[PlayerWidget] Volume set to:" << volume << "(" << vol << ")";
    }
}

int PlayerWidget::volume() const
{
    if (m_mp) {
        return (int)(mp_get_volume(m_mp) * 100);
    }
    return 100;
}

void PlayerWidget::requestPreviewFrame(qint64 position)
{
    // 使用独立的缩略图提取器获取任意位置的帧
    // 不影响播放，异步提取
    if (m_thumbnailExtractor && m_thumbnailExtractor->isOpen()) {
        m_thumbnailExtractor->requestThumbnail(position);
    }
}

void PlayerWidget::setLoopCount(int count)
{
    m_loopCount = count;
    m_currentLoopIndex = 0;
    qDebug() << "[PlayerWidget] Loop count set to:" << count;
}

/*
 * =============================================================================
 * 消息循环线程实现 - 参考 ijkplayer 的 message_loop_n
 * 
 * ijkplayer 的设计：上层（JNI/Qt）驱动消息循环，调用 mp_get_msg()
 * mp_get_msg() 内部处理状态更新，过滤内部消息，只返回通知消息
 * =============================================================================
 */

void PlayerWidget::startMessageLoop()
{
    if (m_msgLoopRunning.load())
        return;
    
    m_msgLoopRunning.store(true);
    m_msgThread = std::thread(messageLoopThread, this);
    qDebug() << "[PlayerWidget] Message loop thread started";
}

void PlayerWidget::stopMessageLoop()
{
    if (!m_msgLoopRunning.load())
        return;
    
    qDebug() << "[PlayerWidget] Stopping message loop thread...";
    
    m_msgLoopRunning.store(false);
    
    // 中止消息队列，唤醒阻塞的 mp_get_msg
    if (m_mp) {
        mp_abort_msg_queue(m_mp);
    }
    
    // 等待线程结束
    if (m_msgThread.joinable()) {
        m_msgThread.join();
    }
    qDebug() << "[PlayerWidget] Message loop thread stopped";
}

void PlayerWidget::messageLoopThread(PlayerWidget *self)
{
    self->messageLoop();
}

/*
 * 消息循环函数 - 参考 ijkplayer 的 message_loop_n
 * 
 * 在独立线程中运行，调用 mp_get_msg() 获取消息
 * mp_get_msg() 已经处理了状态更新和内部消息过滤
 * 这里只需要将返回的通知消息转发到 Qt 主线程
 */
void PlayerWidget::messageLoop()
{
    qDebug() << "[MessageLoop] Thread started";
    
    while (m_msgLoopRunning.load()) {
        if (!m_mp) {
            break;
        }
        
        AVMessage msg;
        
        // 调用 mp_get_msg（ijkplayer 风格的过滤+处理函数）
        // mp_get_msg 内部已处理状态更新，只返回 FFP_MSG_* 通知消息
        int retval = mp_get_msg(m_mp, &msg, 1);  // 阻塞等待
        
        if (retval < 0) {
            // 消息队列被中止
            qDebug() << "[MessageLoop] Message queue aborted";
            break;
        }
        
        if (retval == 0) {
            // 无消息（理论上阻塞模式不应该返回0）
            continue;
        }
        
        // 通过 Qt 信号槽机制（QueuedConnection）将消息发送到主线程处理
        QMetaObject::invokeMethod(this, "onMessage",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, msg.what),
                                  Q_ARG(int, msg.arg1),
                                  Q_ARG(int, msg.arg2));
        
        // 释放消息资源
        msg_free_res(&msg);
    }
    
    qDebug() << "[MessageLoop] Thread exiting";
}

void PlayerWidget::onMessage(int what, int arg1, int arg2)
{
    // 在主线程中处理消息
    switch (what) {
    case FFP_MSG_FLUSH:
        qDebug() << "[MSG] FLUSH";
        break;
        
    case FFP_MSG_ERROR:
        qWarning() << "[MSG] ERROR:" << arg1;
        emit errorOccurred(QString("Playback error: %1").arg(arg1));
        break;
        
    case FFP_MSG_PREPARED:
        qDebug() << "[MSG] PREPARED";
        emit prepared();
        emit durationChanged(mp_get_duration(m_mp));
        // 如果之前调用了 play()（在准备阶段），现在自动开始播放
        if (m_startOnPrepared) {
            qDebug() << "[MSG] Auto-starting playback as requested";
            m_startOnPrepared = false;
            play();
        }
        break;
        
    case FFP_MSG_COMPLETED:
        qDebug() << "[MSG] COMPLETED - loopCount:" << m_loopCount << ", currentLoopIndex:" << m_currentLoopIndex;
        
        // 检查是否需要继续循环
        // loopCount=0: 不循环，播放1次
        // loopCount=1: 循环1次，总共播放2次
        // loopCount=n: 循环n次，总共播放n+1次
        if (m_loopCount > 0 && m_currentLoopIndex < m_loopCount) {
            m_currentLoopIndex++;
            qDebug() << "[MSG] Loop" << m_currentLoopIndex << "/" << m_loopCount 
                     << "- restarting from beginning (total plays:" << (m_currentLoopIndex + 1) << ")";
            if (m_mp) {
                seekTo(0);
                play();
            }
            break;  // 继续循环，不停止
        }
        
        // 播放结束（不循环 或 循环已完成）
        // 发送 completed 信号，由上层 VideoWidget 统一处理停止和 UI 重置
        qDebug() << "[MSG] Playback finished - total plays:" << (m_currentLoopIndex + 1);
        m_currentLoopIndex = 0;  // 重置循环计数
        emit completed();  // 上层 VideoWidget::stopAndReset() 会处理 stop() 和 UI 重置
        break;
        
    case FFP_MSG_VIDEO_SIZE_CHANGED:
        qDebug() << "[MSG] VIDEO_SIZE_CHANGED:" << arg1 << "x" << arg2;
        emit videoSizeChanged(arg1, arg2);
        break;
        
    case FFP_MSG_BUFFERING_START:
        qDebug() << "[MSG] BUFFERING_START";
        emit bufferingStart();
        break;
        
    case FFP_MSG_BUFFERING_END:
        qDebug() << "[MSG] BUFFERING_END";
        emit bufferingEnd();
        break;
        
    case FFP_MSG_BUFFERING_UPDATE:
        emit bufferingUpdate(arg2);
        break;
        
    case FFP_MSG_SEEK_COMPLETE:
        qDebug() << "[MSG] SEEK_COMPLETE:" << arg1;
        emit seekComplete(arg1);
        break;
        
    case FFP_MSG_PLAYBACK_STATE_CHANGED:
        // 检查状态变化
        if (m_mp) {
            int currentState = mp_get_state(m_mp);
            if (currentState != m_lastState) {
                m_lastState = currentState;
                emit stateChanged(currentState);
            }
        }
        break;
        
    case FFP_MSG_VIDEO_RENDERING_START:
        qDebug() << "[MSG] VIDEO_RENDERING_START";
        break;
        
    case FFP_MSG_AUDIO_RENDERING_START:
        qDebug() << "[MSG] AUDIO_RENDERING_START";
        break;
        
    case FFP_MSG_MEDIA_TYPE_CHANGED:
        {
            // arg1 = media_type, arg2 = is_seekable
            m_mediaType = static_cast<MediaType>(arg1);
            m_isSeekable = (arg2 != 0);
            
            const char* typeNames[] = {"Unknown", "File", "VOD", "Live", "Playback"};
            qDebug() << "[MSG] MEDIA_TYPE_CHANGED:" << typeNames[arg1] 
                     << ", seekable:" << m_isSeekable;
            
            emit mediaTypeChanged(m_mediaType, m_isSeekable);
        }
        break;
        
    default:
        // 忽略未处理的消息
        break;
    }
}

void PlayerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
}

void PlayerWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    
    // OpenGL 模式下 VideoGLWidget 会自动调整
    // SDL 模式下可能需要通知 MediaPlayer
}

void PlayerWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        // 空格键：按住用于拖动画面，不自动重复时才处理
        if (!event->isAutoRepeat()) {
            m_spacePressed = true;
            // 如果画面已放大，显示抓手光标
            if (m_videoWidget && m_videoWidget->zoom() > 1.0f) {
                setCursor(Qt::OpenHandCursor);
            }
        }
        break;
    // case Qt::Key_P:
    //     // P 键切换暂停
    //     togglePause();
    //     break;
    // case Qt::Key_Escape:
    // case Qt::Key_Q:
    //     stop();
    //     close();
    //     break;
    // case Qt::Key_F:
    //     if (isFullScreen())
    //         showNormal();
    //     else
    //         showFullScreen();
    //     break;
    // case Qt::Key_M:
    //     // 切换静音
    //     if (m_mp) mp_toggle_mute(m_mp);
    //     break;
    // case Qt::Key_Up:
    //     // 增大音量
    //     if (m_mp) mp_update_volume(m_mp, 1, 10.0);
    //     break;
    // case Qt::Key_Down:
    //     // 减小音量
    //     if (m_mp) mp_update_volume(m_mp, -1, 10.0);
    //     break;
    // case Qt::Key_Left:
    //     // 后退 10 秒
    //     if (m_mp) mp_seek_relative(m_mp, -10.0);
    //     break;
    // case Qt::Key_Right:
    //     // 前进 10 秒
    //     if (m_mp) mp_seek_relative(m_mp, 10.0);
    //     break;
    // case Qt::Key_R:
    //     // R 键重置视图
    //     if (m_videoWidget) {
    //         m_videoWidget->resetView();
    //     }
    //     break;
    // default:
    //     QWidget::keyPressEvent(event);
    //     break;
    }
}

void PlayerWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
    } else {
        QWidget::keyReleaseEvent(event);
    }
}

void PlayerWidget::wheelEvent(QWheelEvent *event)
{
    // Ctrl + 滚轮缩放画面
    if ((event->modifiers() & Qt::ControlModifier) && m_videoWidget) {
        QPoint numDegrees = event->angleDelta();
        if (!numDegrees.isNull()) {
            float currentZoom = m_videoWidget->zoom();
            float delta = numDegrees.y() > 0 ? ZOOM_STEP : -ZOOM_STEP;
            float newZoom = currentZoom + delta * currentZoom;
            
            // 获取鼠标位置
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            QPointF mousePos = event->position();
#else
            QPointF mousePos = event->posF();
#endif
            // 转换为 VideoGLWidget 的坐标
            QPointF localPos = m_videoWidget->mapFromParent(mousePos.toPoint());
            
            // 以鼠标位置为中心缩放
            m_videoWidget->zoomAt(newZoom, localPos);
        }
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

void PlayerWidget::mousePressEvent(QMouseEvent *event)
{
    // 空格 + 左键开始拖动画面
    if (m_spacePressed && event->button() == Qt::LeftButton && m_videoWidget) {
        if (m_videoWidget->zoom() > 1.0f) {
            m_isPanning = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void PlayerWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isPanning && m_videoWidget) {
        // 计算鼠标移动距离
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        
        // 更新平移偏移
        QPointF currentPan = m_videoWidget->pan();
        m_videoWidget->setPan(currentPan + QPointF(delta));
        
        event->accept();
    } else if (m_spacePressed && m_videoWidget && m_videoWidget->zoom() > 1.0f) {
        // 空格按下时显示抓手光标
        setCursor(Qt::OpenHandCursor);
    }
}

void PlayerWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_isPanning && event->button() == Qt::LeftButton) {
        m_isPanning = false;
        if (m_spacePressed && m_videoWidget && m_videoWidget->zoom() > 1.0f) {
            setCursor(Qt::OpenHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void PlayerWidget::focusOutEvent(QFocusEvent *event)
{
    // 失去焦点时重置交互状态
    m_spacePressed = false;
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    QWidget::focusOutEvent(event);
}

/*
 * =============================================================================
 * 硬件加速控制
 * =============================================================================
 */

void PlayerWidget::setHWAccelType(HWAccelType type)
{
    m_hwAccelType = type;
    
    if (m_mp) {
        // 设置硬解码类型，下次打开文件时生效
        mp_set_hwaccel_type(m_mp, static_cast<MPHWAccelType>(static_cast<int>(type)));
        qDebug() << "[PlayerWidget] HWAccel type set to:" << hwAccelName(type);
    }
}

PlayerWidget::HWAccelType PlayerWidget::hwAccelType() const
{
    return m_hwAccelType;
}

QList<std::tuple<PlayerWidget::HWAccelType, QString, bool>> PlayerWidget::availableHWAccels()
{
    QList<std::tuple<HWAccelType, QString, bool>> result;
    
    MPHWAccelInfo infos[MP_HWACCEL_COUNT];
    int count = mp_get_available_hwaccels(infos, MP_HWACCEL_COUNT);
    
    for (int i = 0; i < count; i++) {
        HWAccelType type = static_cast<HWAccelType>(static_cast<int>(infos[i].type));
        QString name = QString::fromUtf8(infos[i].description);
        bool available = infos[i].available != 0;
        result.append(std::make_tuple(type, name, available));
    }
    
    return result;
}

bool PlayerWidget::isHWAccelAvailable(HWAccelType type)
{
    return mp_is_hwaccel_available(static_cast<MPHWAccelType>(static_cast<int>(type))) != 0;
}

QString PlayerWidget::hwAccelName(HWAccelType type)
{
    const char *name = mp_get_hwaccel_name(static_cast<MPHWAccelType>(static_cast<int>(type)));
    return QString::fromUtf8(name);
}

/*
 * =============================================================================
 * 媒体类型相关方法
 * =============================================================================
 */

PlayerWidget::MediaType PlayerWidget::mediaType() const
{
    return m_mediaType;
}

bool PlayerWidget::isLive() const
{
    return m_mediaType == MediaType::Live;
}

bool PlayerWidget::isSeekable() const
{
    return m_isSeekable;
}

void PlayerWidget::setLiveLowLatency(bool enabled)
{
    if (m_mp) {
        mp_set_live_low_latency(m_mp, enabled ? 1 : 0);
        qDebug() << "[PlayerWidget] Live low latency:" << (enabled ? "enabled" : "disabled");
    }
}

void PlayerWidget::setTimeout(int timeoutMs)
{
    if (m_mp) {
        mp_set_timeout(m_mp, timeoutMs);
        qDebug() << "[PlayerWidget] Timeout set to:" << timeoutMs << "ms";
    }
}
