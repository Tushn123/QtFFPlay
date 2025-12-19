#include "VideoWidget.h"
#include <QDebug>
#include <QAbstractItemView>

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

    // ============ 连接播放器和工具栏信号 ============
    
    // 播放时长变化 -> 更新工具栏
    connect(playerWidget, &PlayerWidget::durationChanged, 
            videoToolBarWidget, &VideoToolBarWidget::setDuration);
    
    // 播放位置变化 -> 更新工具栏
    connect(playerWidget, &PlayerWidget::positionChanged,
            videoToolBarWidget, &VideoToolBarWidget::setCurrentTime);
    
    // 工具栏播放/暂停按钮 -> 控制播放器
    connect(videoToolBarWidget, &VideoToolBarWidget::playPauseClicked,
            playerWidget, &PlayerWidget::togglePause);
    
    // 工具栏逐帧前进按钮 -> 控制播放器
    connect(videoToolBarWidget, &VideoToolBarWidget::stepForwardClicked,
            this, [this]() {
        qDebug() << "[VideoWidget] Step forward clicked";
        playerWidget->stepForward();
        // 逐帧播放后视频会变成暂停状态，更新工具栏显示
        videoToolBarWidget->setPlaying(false);
    });
    
    // 工具栏逐帧后退按钮（暂不支持，提示用户）
    connect(videoToolBarWidget, &VideoToolBarWidget::stepBackwardClicked,
            this, [this]() {
        qDebug() << "[VideoWidget] Step backward not supported yet";
        // TODO: 实现逐帧后退功能（需要解码器支持）
    });
    
    // 工具栏进度条拖动/点击 -> 播放器 seek
    connect(videoToolBarWidget, &VideoToolBarWidget::seekRequested,
            this, [this](qint64 position) {
        qDebug() << "[VideoWidget] Seek requested to:" << position << "ms";
        playerWidget->seekTo(static_cast<long>(position));
    });
    
    // 工具栏倍速选择 -> 播放器倍速设置
    connect(videoToolBarWidget, &VideoToolBarWidget::speedChanged,
            this, [this](float speed) {
        qDebug() << "[VideoWidget] Speed changed to:" << speed;
        playerWidget->setPlaybackRate(speed);
    });
    
    // 工具栏音量调整 -> 播放器音量设置
    connect(videoToolBarWidget, &VideoToolBarWidget::volumeChanged,
            this, [this](int volume) {
        qDebug() << "[VideoWidget] Volume changed to:" << volume;
        playerWidget->setVolume(volume);
    });
    
    // 标题栏缩放模式选择 -> 视频渲染组件
    connect(videoTitleBarWidget, &VideoTitleBarWidget::scaleModeChanged,
            this, [this](ScaleMode mode) {
        const char* modeNames[] = {"Fit", "Stretch", "Fill"};
        qDebug() << "[VideoWidget] Scale mode changed to:" << modeNames[static_cast<int>(mode)];
        if (playerWidget->videoWidget()) {
            playerWidget->videoWidget()->setScaleMode(mode);
        }
    });
    
    // 标题栏硬件加速类型选择 -> 播放器（注意：需重新加载视频才能生效）
    connect(videoTitleBarWidget, &VideoTitleBarWidget::hwAccelTypeChanged,
            this, [this](PlayerWidget::HWAccelType type) {
        QString typeName = PlayerWidget::hwAccelName(type);
        qDebug() << "[VideoWidget] HWAccel type changed to:" << typeName;
        playerWidget->setHWAccelType(type);
        
        // 注意：硬件加速类型改变后需要重新加载视频才能生效
        // 这里可以选择自动重新加载或提示用户
    });

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
    
    // 检查是否有弹出控件正在显示
    // 如果有，则不隐藏工具栏（避免闪动）
    
    // 检查标题栏的下拉框状态
    if (videoTitleBarWidget) {
        QComboBox *scaleModeCombo = videoTitleBarWidget->scaleModeCombo;
        QComboBox *hwAccelCombo = videoTitleBarWidget->hwAccelCombo;
        
        if ((scaleModeCombo && scaleModeCombo->view() && scaleModeCombo->view()->isVisible()) ||
            (hwAccelCombo && hwAccelCombo->view() && hwAccelCombo->view()->isVisible())) {
            return;
        }
    }
    
    // 检查底部工具栏的下拉框状态
    if (videoToolBarWidget) {
        // 检查 speedCombo 和 resolutionCombo 的下拉框状态
        QComboBox *speedCombo = videoToolBarWidget->speedCombo;
        QComboBox *resolutionCombo = videoToolBarWidget->resolutionCombo;
        
        // 如果任一 combo box 正在显示下拉列表，则不隐藏工具栏
        if ((speedCombo && speedCombo->view() && speedCombo->view()->isVisible()) ||
            (resolutionCombo && resolutionCombo->view() && resolutionCombo->view()->isVisible())) {
            return;
        }
        
        // 检查音量弹出框是否显示
        if (videoToolBarWidget->volumePopup && videoToolBarWidget->volumePopup->isVisible()) {
            return;
        }
    }
    
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
