#include "VideoWidget.h"
#include <QDebug>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget{parent}
{
    initUi();
}

void VideoWidget::initUi()
{
    videoTitleBarWidget = new VideoTitleBarWidget(this);
    playerWidget = new PlayerWidget(this);
    videoToolBarWidget = new VideoToolBarWidget(this);

    // 主布局
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 创建一个容器用于承载播放器
    QWidget* playerContainer = new QWidget(this);
    QVBoxLayout* containerLayout = new QVBoxLayout(playerContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);
    containerLayout->addWidget(playerWidget);

    // 将容器添加到主布局
    mainLayout->addWidget(playerContainer);

    // 设置控件属性
    videoTitleBarWidget->setFixedHeight(40);
    videoToolBarWidget->setFixedHeight(60);

    // 设置顶部工具栏的位置
    videoTitleBarWidget->move(0, 0);

    // 初始状态
    videoTitleBarWidget->hide();
    videoToolBarWidget->hide();

    // 启用鼠标追踪
    setMouseTracking(true);
    videoTitleBarWidget->raise();
    videoToolBarWidget->raise();

    // 设置默认媒体文件
    QString mediaPath = "C:/shn/media/animal.mp4";
    playerWidget->setMedia(mediaPath);
    playerWidget->play();

    // 设置底部工具栏的位置（需要在resizeEvent中调整）
    updateBarPosition();
}

void VideoWidget::enterEvent(QEvent* event) {
    QWidget::enterEvent(event);
    videoTitleBarWidget->show();
    videoToolBarWidget->show();
}

void VideoWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    videoTitleBarWidget->hide();
    videoToolBarWidget->hide();
}

void VideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateBarPosition();
}

int VideoWidget::getId() const
{
    return id;
}

void VideoWidget::setId(int newId)
{
    id = newId;
}

void VideoWidget::updateBarPosition()
{
    if (videoTitleBarWidget){
        // 设置顶部工具栏位置
        videoTitleBarWidget->setGeometry(
            0,
            0,
            width(),
            videoTitleBarWidget->height()
            );
    }

    if (videoToolBarWidget){
        // 设置底部工具栏位置
        videoToolBarWidget->setGeometry(
            0,
            height() - videoToolBarWidget->height(),
            width(),
            videoToolBarWidget->height()
            );
    }
}
