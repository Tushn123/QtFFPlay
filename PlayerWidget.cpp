/*
 * Copyright (c) 2024 FFPlayer contributors
 *
 * PlayerWidget 实现 - 使用 MediaPlayer 层
 * 参考 ijkplayer 的 message_loop_n 实现消息循环
 */

#include "PlayerWidget.h"
#include <QKeyEvent>
#include <QDebug>
#include <QPainter>
#include <QMetaObject>

// 避免 C 头文件中的 class 关键字冲突
#define class class_name
extern "C" {
#include "player/mediaplayer.h"
#include "player/ff_ffmsg.h"
#include "player/ff_ffmsg_queue.h"
}
#undef class

PlayerWidget::PlayerWidget(QWidget *parent)
    : QWidget(parent)
    , m_mp(nullptr)
    , m_initialized(false)
    , m_msgLoopRunning(false)
    , m_lastState(MP_STATE_IDLE)
    , m_startOnPrepared(false)
{
    // 确保 widget 有原生窗口句柄
    setAttribute(Qt::WA_NativeWindow);
    
    // 设置焦点策略以接收键盘事件
    setFocusPolicy(Qt::StrongFocus);
    
    // 设置最小尺寸
    setMinimumSize(320, 240);
    
    // 连接消息处理信号槽（使用 QueuedConnection 跨线程）
    connect(this, &PlayerWidget::stateChanged, this, [this](int state) {
        qDebug() << "[PlayerWidget] State changed to:" << state;
    });
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
    
    m_initialized = true;
    m_lastState = MP_STATE_IDLE;
    qDebug() << "MediaPlayer initialized, state:" << mp_get_state(m_mp);
}

void PlayerWidget::cleanupPlayer()
{
    qDebug() << "[PlayerWidget] cleanupPlayer called";
    
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
    
    mp_global_uninit();
    m_initialized = false;
    qDebug() << "[PlayerWidget] cleanupPlayer done";
}

void PlayerWidget::setMedia(const QString &path)
{
    m_mediaPath = path;
    qDebug() << "setMedia:" << path;
    
    // 重置自动播放标志
    m_startOnPrepared = false;
    
    // 确保播放器已初始化
    initPlayer();
    
    if (!m_mp) {
        qWarning() << "MediaPlayer not initialized";
        return;
    }
    
    // 附加窗口
    if (mp_attach_window(m_mp, (void*)winId(), width(), height()) < 0) {
        qWarning() << "Failed to attach window";
        emit errorOccurred("Failed to attach window");
        return;
    }
    
    qDebug() << "Window attached successfully";
    
    // 设置数据源
    if (mp_set_data_source(m_mp, m_mediaPath.toUtf8().constData()) < 0) {
        qWarning() << "Failed to set data source:" << m_mediaPath;
        emit errorOccurred("Failed to set data source: " + m_mediaPath);
        return;
    }
    
    qDebug() << "Data source set, state:" << mp_get_state(m_mp);
    
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
    if (!m_mp) {
        return;
    }
    
    int state = mp_get_state(m_mp);
    
    // 如果还在准备中，设置标志，等准备完成后自动播放
    if (state == MP_STATE_ASYNC_PREPARING) {
        qDebug() << "[PlayerWidget] Still preparing, will start on prepared";
        m_startOnPrepared = true;
        return;
    }
    
    // 其他状态，直接调用 mp_start
    int ret = mp_start(m_mp);
    qDebug() << "mp_start returned:" << ret << ", state:" << state;
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
    if (m_mp) {
        int ret = mp_stop(m_mp);
        qDebug() << "mp_stop returned:" << ret << ", state:" << mp_get_state(m_mp);
    }
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
        qDebug() << "[MSG] COMPLETED";
        emit completed();
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
        
    default:
        // 忽略未处理的消息
        break;
    }
}

void PlayerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
}

void PlayerWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        togglePause();
        break;
    case Qt::Key_Escape:
    case Qt::Key_Q:
        stop();
        close();
        break;
    case Qt::Key_F:
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
        break;
    case Qt::Key_M:
        // 切换静音
        if (m_mp) mp_toggle_mute(m_mp);
        break;
    case Qt::Key_Up:
        // 增大音量
        if (m_mp) mp_update_volume(m_mp, 1, 10.0);
        break;
    case Qt::Key_Down:
        // 减小音量
        if (m_mp) mp_update_volume(m_mp, -1, 10.0);
        break;
    case Qt::Key_Left:
        // 后退 10 秒
        if (m_mp) mp_seek_relative(m_mp, -10.0);
        break;
    case Qt::Key_Right:
        // 前进 10 秒
        if (m_mp) mp_seek_relative(m_mp, 10.0);
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}
