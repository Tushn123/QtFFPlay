/*
 * Copyright (c) 2024 FFPlayer contributors
 *
 * PlayerWidget 实现 - 简化版
 */

#include "PlayerWidget.h"
#include <QKeyEvent>
#include <QDebug>

// 避免 C 头文件中的 class 关键字冲突
#define class class_name
extern "C" {
#include "player/ff_ffplayer.h"
}
#undef class

PlayerWidget::PlayerWidget(QWidget *parent)
    : QWidget(parent)
    , m_ffp(nullptr)
    , m_initialized(false)
{
    // 确保 widget 有原生窗口句柄（子窗口需要一个父窗口句柄）
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    
    // 设置焦点策略以接收键盘事件
    setFocusPolicy(Qt::StrongFocus);
    
    // 设置最小尺寸
    setMinimumSize(320, 240);
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
    ffp_global_init();
    
    // 创建播放器实例
    m_ffp = ffp_create();
    if (!m_ffp) {
        qWarning() << "Failed to create FFPlayer instance";
        emit errorOccurred("Failed to create FFPlayer instance");
        return;
    }
    
    m_initialized = true;
}

void PlayerWidget::cleanupPlayer()
{
    if (m_ffp) {
        ffp_shutdown(m_ffp);
        m_ffp = nullptr;
    }
    
    ffp_global_uninit();
    m_initialized = false;
}

void PlayerWidget::setMedia(const QString &path)
{
    m_mediaPath = path;
    qDebug() << "setMedia:" << path;
    
    // 确保播放器已初始化
    initPlayer();
    
    if (!m_ffp) {
        qWarning() << "FFPlayer not initialized";
        return;
    }
    
    // 一键附加窗口（FFPlayer 内部处理所有 SDL 逻辑）
    if (ffp_attach_window(m_ffp, (void*)winId(), width(), height()) < 0) {
        qWarning() << "Failed to attach window";
        emit errorOccurred("Failed to attach window");
        return;
    }
    
    qDebug() << "Window attached successfully";
    
    // 准备并开始播放
    if (ffp_prepare_async(m_ffp, m_mediaPath.toUtf8().constData()) < 0) {
        qWarning() << "Failed to prepare media:" << m_mediaPath;
        emit errorOccurred("Failed to prepare media: " + m_mediaPath);
        return;
    }
    
    qDebug() << "Media prepared and playing";
}

void PlayerWidget::play()
{
    if (m_ffp) {
        ffp_start(m_ffp);
    }
}

void PlayerWidget::pause()
{
    if (m_ffp) {
        ffp_pause(m_ffp);
    }
}

void PlayerWidget::stop()
{
    if (m_ffp) {
        ffp_stop(m_ffp);
    }
}

void PlayerWidget::togglePause()
{
    if (m_ffp) {
        ffp_toggle_pause(m_ffp);
    }
}

bool PlayerWidget::isPlaying() const
{
    if (m_ffp) {
        return ffp_is_playing(m_ffp);
    }
    return false;
}

bool PlayerWidget::isPaused() const
{
    if (m_ffp) {
        return ffp_is_paused(m_ffp);
    }
    return true;
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
        if (m_ffp) ffp_toggle_mute(m_ffp);
        break;
    case Qt::Key_Up:
        if (m_ffp) ffp_update_volume(m_ffp, 1, 10.0);
        break;
    case Qt::Key_Down:
        if (m_ffp) ffp_update_volume(m_ffp, -1, 10.0);
        break;
    case Qt::Key_Left:
        if (m_ffp) ffp_seek_relative(m_ffp, -10.0);
        break;
    case Qt::Key_Right:
        if (m_ffp) ffp_seek_relative(m_ffp, 10.0);
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}
