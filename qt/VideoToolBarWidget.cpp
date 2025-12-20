#include "VideoToolBarWidget.h"

#include <QVBoxLayout>
#include <QApplication>
#include <QMouseEvent>
#include <QStyleOptionSlider>
#include <QDebug>
#include <QPalette>

// ============ ProgressTooltip 实现 ============

ProgressTooltip::ProgressTooltip(QWidget *parent)
    : QFrame(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);  // 显示时不抢焦点
    
    // 计算总尺寸：预览图 + 边距 + 时间标签
    int totalWidth = PREVIEW_WIDTH + 8;   // 预览宽度 + 左右边距
    int totalHeight = PREVIEW_HEIGHT + 30; // 预览高度 + 时间标签 + 边距
    setFixedSize(totalWidth, totalHeight);
    
    // 不使用类名选择器，直接设置样式
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 30, 240));
    setPalette(pal);
    
    setStyleSheet(R"(
        QFrame {
            background-color: rgba(30, 30, 30, 240);
            border: 1px solid #555555;
            border-radius: 6px;
        }
        QLabel {
            color: white;
            background: transparent;
            border: none;
        }
    )");
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    
    // 预览画面标签
    m_previewLabel = new QLabel(this);
    m_previewLabel->setFixedSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(R"(
        QLabel {
            background-color: #1a1a1a;
            border: 1px solid #444444;
            border-radius: 3px;
        }
    )");
    // m_previewLabel->setText("预览");  // 默认显示文字
    
    // 时间标签
    m_timeLabel = new QLabel("00:00", this);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setStyleSheet(R"(
        QLabel {
            font-size: 12px;
            font-weight: bold;
            color: #ffffff;
        }
    )");
    
    layout->addWidget(m_previewLabel);
    layout->addWidget(m_timeLabel);
}

void ProgressTooltip::setTime(qint64 milliseconds)
{
    if (milliseconds < 0) milliseconds = 0;
    
    int totalSeconds = milliseconds / 1000;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    
    if (hours > 0) {
        m_timeLabel->setText(QString("%1:%2:%3")
                             .arg(hours)
                             .arg(minutes, 2, 10, QChar('0'))
                             .arg(seconds, 2, 10, QChar('0')));
    } else {
        m_timeLabel->setText(QString("%1:%2")
                             .arg(minutes, 2, 10, QChar('0'))
                             .arg(seconds, 2, 10, QChar('0')));
    }
}

void ProgressTooltip::setPreviewImage(const QImage &image)
{
    if (image.isNull()) {
        m_previewLabel->setText("预览");
        return;
    }
    
    // 缩放图片以适应预览区域，保持宽高比
    QPixmap pixmap = QPixmap::fromImage(image).scaled(
        PREVIEW_WIDTH, PREVIEW_HEIGHT,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    m_previewLabel->setPixmap(pixmap);
}

void ProgressTooltip::clearPreview()
{
    m_previewLabel->clear();
    m_previewLabel->setText("预览");
}

void ProgressTooltip::showAt(const QPoint &globalPos)
{
    // 显示在指定位置上方
    int x = globalPos.x() - width() / 2;
    int y = globalPos.y() - height() - 12;
    
    move(x, y);
    
    if (!isVisible()) {
        show();
        raise();
    }
}

// ============ VideoProgressSlider 实现 ============

VideoProgressSlider::VideoProgressSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
    , m_tooltip(new ProgressTooltip(nullptr))  // 无父窗口，独立显示
    , m_duration(0)
    , m_isDragging(false)
    , m_lastPreviewPos(-1)
    , m_previewDelayTimer(new QTimer(this))
    , m_pendingPreviewPos(-1)
{
    setMouseTracking(true);
    setRange(0, 1000);
    
    // 预览延迟定时器（单次触发）
    m_previewDelayTimer->setSingleShot(true);
    connect(m_previewDelayTimer, &QTimer::timeout, this, &VideoProgressSlider::onPreviewDelayTimeout);
    
    // 进度条样式
    setStyleSheet(R"(
        QSlider::groove:horizontal {
            background: #404040;
            height: 6px;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #0088ff;
            border: none;
            width: 14px;
            height: 14px;
            margin: -4px 0;
            border-radius: 7px;
        }
        QSlider::handle:horizontal:hover {
            background: #00aaff;
            width: 16px;
            height: 16px;
            margin: -5px 0;
            border-radius: 8px;
        }
        QSlider::sub-page:horizontal {
            background: #0088ff;
            border-radius: 3px;
        }
        QSlider::add-page:horizontal {
            background: #404040;
            border-radius: 3px;
        }
    )");
}

VideoProgressSlider::~VideoProgressSlider()
{
    if (m_tooltip) {
        m_tooltip->hide();
        delete m_tooltip;
        m_tooltip = nullptr;
    }
}

void VideoProgressSlider::setDuration(qint64 milliseconds)
{
    m_duration = milliseconds;
    qDebug() << "[VideoProgressSlider] Duration set to:" << milliseconds << "ms";
}

qint64 VideoProgressSlider::positionFromMouse(int x) const
{
    if (m_duration <= 0 || width() <= 0) {
        return 0;
    }
    
    // 简单直接的计算：使用控件宽度作为基准
    // 考虑一些边距（大约 7 像素的 handle 半径）
    int effectiveWidth = width();
    int margin = 7;  // handle 半径
    
    // 限制 x 在有效范围内
    x = qBound(margin, x, effectiveWidth - margin);
    
    // 计算比例 (0.0 ~ 1.0)
    double ratio = (double)(x - margin) / (effectiveWidth - 2 * margin);
    ratio = qBound(0.0, ratio, 1.0);
    
    // 返回对应的毫秒位置
    return (qint64)(ratio * m_duration);
}

int VideoProgressSlider::valueFromPosition(int x) const
{
    if (width() <= 0) return 0;
    
    int effectiveWidth = width();
    int margin = 7;
    
    x = qBound(margin, x, effectiveWidth - margin);
    double ratio = (double)(x - margin) / (effectiveWidth - 2 * margin);
    ratio = qBound(0.0, ratio, 1.0);
    
    return (int)(ratio * maximum());
}

void VideoProgressSlider::updateTooltip(int x)
{
    if (m_duration <= 0 || !m_tooltip) {
        if (m_tooltip) m_tooltip->hide();
        return;
    }
    
    qint64 position = positionFromMouse(x);
    m_tooltip->setTime(position);
    
    // 计算显示位置（在进度条上方）
    QPoint globalPos = mapToGlobal(QPoint(x, 0));
    m_tooltip->showAt(globalPos);
    
    // 延迟请求预览帧（鼠标悬停超过 200ms 才解码）
    // 避免快速移动时频繁解码影响播放性能
    if (qAbs(position - m_lastPreviewPos) > 500) {
        m_pendingPreviewPos = position;
        m_previewDelayTimer->start(PREVIEW_DELAY_MS);
    }
}

void VideoProgressSlider::onPreviewDelayTimeout()
{
    // 延迟时间到，发送预览请求
    if (m_pendingPreviewPos >= 0 && m_pendingPreviewPos != m_lastPreviewPos) {
        m_lastPreviewPos = m_pendingPreviewPos;
        emit previewRequested(m_pendingPreviewPos);
    }
}

void VideoProgressSlider::setPreviewImage(const QImage &image)
{
    if (m_tooltip) {
        m_tooltip->setPreviewImage(image);
    }
}

void VideoProgressSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        
        // 直接计算点击位置对应的值并设置
        int newValue = valueFromPosition(event->pos().x());
        setValue(newValue);
        
        // 如果有 duration，发送 seek 请求
        if (m_duration > 0) {
            qint64 position = positionFromMouse(event->pos().x());
            emit seekRequested(position);
        }
        
        emit sliderPressed();
        event->accept();
    } else {
        QSlider::mousePressEvent(event);
    }
}

void VideoProgressSlider::mouseMoveEvent(QMouseEvent *event)
{
    // 始终更新时间提示
    updateTooltip(event->pos().x());
    
    if (m_isDragging) {
        // 拖动时更新滑块位置
        int newValue = valueFromPosition(event->pos().x());
        setValue(newValue);
        event->accept();
    } else {
        QSlider::mouseMoveEvent(event);
    }
}

void VideoProgressSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        
        // 发送最终跳转请求
        if (m_duration > 0) {
            qint64 position = positionFromMouse(event->pos().x());
            emit seekRequested(position);
        }
        
        emit sliderReleased();
        event->accept();
    } else {
        QSlider::mouseReleaseEvent(event);
    }
}

void VideoProgressSlider::enterEvent(QEvent *event)
{
    QSlider::enterEvent(event);
}

void VideoProgressSlider::leaveEvent(QEvent *event)
{
    QSlider::leaveEvent(event);
    
    // 停止预览延迟定时器
    m_previewDelayTimer->stop();
    m_pendingPreviewPos = -1;
    
    if (m_tooltip) {
        m_tooltip->hide();
        m_tooltip->clearPreview();
    }
    m_lastPreviewPos = -1;  // 重置，下次进入时重新请求
}

// ============ VolumePopup 实现 ============

VolumePopup::VolumePopup(QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setFixedSize(50, 180);
    
    // 设置样式
    setStyleSheet(R"(
        VolumePopup {
            background-color: rgba(50, 50, 50, 230);
            border: 1px solid #555555;
            border-radius: 8px;
        }
    )");
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 12, 8, 12);
    layout->setSpacing(8);
    
    // 音量数值标签
    m_label = new QLabel("100", this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet("color: white; font-size: 14px; font-weight: bold;");
    m_label->setFixedHeight(20);
    
    // 垂直音量滑块
    m_slider = new QSlider(Qt::Vertical, this);
    m_slider->setRange(0, 100);
    m_slider->setValue(100);
    m_slider->setStyleSheet(R"(
        QSlider::groove:vertical {
            background: #444444;
            width: 6px;
            border-radius: 3px;
        }
        QSlider::handle:vertical {
            background: #0088ff;
            border: none;
            height: 14px;
            width: 14px;
            margin: 0 -4px;
            border-radius: 7px;
        }
        QSlider::handle:vertical:hover {
            background: #00aaff;
        }
        QSlider::sub-page:vertical {
            background: #444444;
            border-radius: 3px;
        }
        QSlider::add-page:vertical {
            background: #0088ff;
            border-radius: 3px;
        }
    )");
    
    layout->addWidget(m_label);
    layout->addWidget(m_slider, 1);
    
    // 连接信号
    connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
        m_label->setText(QString::number(value));
        emit volumeChanged(value);
    });
    
    // 安装事件过滤器以便点击外部时关闭
    qApp->installEventFilter(this);
}

void VolumePopup::setVolume(int volume)
{
    m_slider->blockSignals(true);
    m_slider->setValue(volume);
    m_label->setText(QString::number(volume));
    m_slider->blockSignals(false);
}

int VolumePopup::volume() const
{
    return m_slider->value();
}

void VolumePopup::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
}

void VolumePopup::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
}

bool VolumePopup::eventFilter(QObject *watched, QEvent *event)
{
    // 点击外部区域时关闭弹窗
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (!geometry().contains(mouseEvent->globalPos())) {
            hide();
        }
    }
    return QFrame::eventFilter(watched, event);
}

// ============ VideoToolBarWidget 实现 ============

VideoToolBarWidget::VideoToolBarWidget(QWidget *parent)
    : QWidget{parent}
    , isPlaying_(false)
    , currentTime_(0)
    , duration_(0)
    , volume_(0)
    , muted_(false)
{
    initUI();
    initConnect();
}

void VideoToolBarWidget::initUI()
{
    this->setAttribute(Qt::WA_StyledBackground, true);
    this->setAutoFillBackground(true);
    this->setStyleSheet(
        "VideoToolBarWidget {"
        "  background: qlineargradient("
        "    x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 rgba(0, 0, 0, 220),"  // 顶部的黑色
        "    stop:1 rgba(0, 0, 0, 180)"   // 底部的稍透明的黑色
        "  );"
        "}"
        );

    // 创建主垂直布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 5, 10, 5);
    mainLayout->setSpacing(5);

    // ============ 上方区域：时间显示和进度条 ============
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setSpacing(10);

    // 时间标签
    timeLabel = new QLabel("00:00 / 00:00", this);
    timeLabel->setStyleSheet("color: white; font-size: 12px;");
    timeLabel->setMinimumWidth(120);  // 支持 h:mm:ss / h:mm:ss 格式

    // 进度条（使用自定义进度条，支持点击跳转和时间提示）
    progressSlider = new VideoProgressSlider(this);
    progressSlider->setMinimumHeight(20);

    // 添加上方区域控件
    topLayout->addWidget(timeLabel, 0, Qt::AlignLeft);
    topLayout->addWidget(progressSlider, 1); // 拉伸因子为1，撑满剩余宽度

    // ============ 下方区域：播放控制按钮 ============
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(10);

    // 左侧：播放控制按钮区域
    QHBoxLayout *leftControlLayout = new QHBoxLayout();
    leftControlLayout->setSpacing(5);

    // 播放/暂停按钮
    playPauseButton = new QPushButton("▶", this);
    playPauseButton->setFixedSize(40, 30);
    playPauseButton->setToolTip("播放/暂停");
    playPauseButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0066cc;
            color: white;
            border: 1px solid #0055aa;
            border-radius: 4px;
        }
        QPushButton:hover {
            background-color: #0077dd;
        }
        QPushButton:pressed {
            background-color: #0055bb;
        }
    )");

    // 逐帧正放按钮
    stepForwardButton = new QPushButton(">>", this);
    stepForwardButton->setFixedSize(30, 30);
    stepForwardButton->setToolTip("逐帧正放");
    stepForwardButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: 1px solid #555555;
            border-radius: 4px;
        }
        QPushButton:hover {
            background-color: #444444;
        }
        QPushButton:pressed {
            background-color: #222222;
        }
    )");

    // 停止按钮
    stopButton = new QPushButton("■", this);
    stopButton->setFixedSize(30, 30);
    stopButton->setToolTip("停止");
    stopButton->setStyleSheet(R"(
        QPushButton {
            background-color: #cc3333;
            color: white;
            border: 1px solid #aa2222;
            border-radius: 4px;
            font-size: 14px;
        }
        QPushButton:hover {
            background-color: #dd4444;
        }
        QPushButton:pressed {
            background-color: #bb2222;
        }
    )");

    // 添加左侧控件
    leftControlLayout->addWidget(playPauseButton);
    leftControlLayout->addWidget(stepForwardButton);
    leftControlLayout->addWidget(stopButton);

    // 中间：弹簧，实现两端对齐
    QSpacerItem *horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    // 右侧：倍速和分辨率选择区域
    QHBoxLayout *rightControlLayout = new QHBoxLayout();
    rightControlLayout->setSpacing(10);

    // 音量按钮
    volumeButton = new QPushButton("🔊", this);
    volumeButton->setFixedSize(36, 30);
    volumeButton->setToolTip("音量");
    volumeButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: 1px solid #555555;
            border-radius: 4px;
            font-size: 16px;
        }
        QPushButton:hover {
            background-color: #444444;
        }
        QPushButton:pressed {
            background-color: #222222;
        }
    )");
    
    // 音量弹出控件
    volumePopup = new VolumePopup(this);
    volumePopup->setVolume(100);
    volumePopup->hide();

    speedCombo = new QComboBox(this);
    speedCombo->setFixedWidth(60);
    speedCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #333333;
            color: white;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 3px;
        }
        QComboBox:hover {
            background-color: #444444;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox QAbstractItemView {
            background-color: #333333;
            color: white;
            selection-background-color: #0066cc;
        }
    )");

    // 添加倍速选项 (0.5 ~ 2.0)
    QStringList speedOptions = {"0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "1.75x", "2.0x"};
    for (const QString &option : speedOptions) {
        speedCombo->addItem(option);
    }
    // 默认选择1.0x
    speedCombo->setCurrentIndex(2);

    resolutionCombo = new QComboBox(this);
    resolutionCombo->setFixedWidth(70);
    resolutionCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #333333;
            color: white;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 3px;
        }
        QComboBox:hover {
            background-color: #444444;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox QAbstractItemView {
            background-color: #333333;
            color: white;
            selection-background-color: #0066cc;
        }
    )");

    // 添加分辨率选项
    QStringList resolutionOptions = {"1080P", "720P", "360P"};
    for (const QString &option : resolutionOptions) {
        resolutionCombo->addItem(option);
    }

    // 添加右侧控件
    rightControlLayout->addWidget(volumeButton);
    rightControlLayout->addWidget(speedCombo);
    rightControlLayout->addWidget(resolutionCombo);

    // 组装下方布局
    bottomLayout->addLayout(leftControlLayout, 0);
    bottomLayout->addItem(horizontalSpacer);
    bottomLayout->addLayout(rightControlLayout, 0);

    // ============ 组装主布局 ============
    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(bottomLayout);

    // 设置固定高度
    this->setFixedHeight(100);

}

void VideoToolBarWidget::initConnect()
{
    // 播放控制按钮
    connect(playPauseButton, &QPushButton::clicked, this, &VideoToolBarWidget::onPlayPauseClicked);
    connect(stepForwardButton, &QPushButton::clicked, this, &VideoToolBarWidget::stepForwardClicked);
    connect(stopButton, &QPushButton::clicked, this, &VideoToolBarWidget::stopClicked);

    // 进度条
    connect(progressSlider, &QSlider::valueChanged, this, &VideoToolBarWidget::onProgressSliderChanged);
    connect(progressSlider, &QSlider::sliderPressed, this, &VideoToolBarWidget::onProgressSliderPressed);
    connect(progressSlider, &QSlider::sliderReleased, this, &VideoToolBarWidget::onProgressSliderReleased);
    connect(progressSlider, &VideoProgressSlider::seekRequested, this, &VideoToolBarWidget::seekRequested);
    connect(progressSlider, &VideoProgressSlider::previewRequested, this, &VideoToolBarWidget::previewRequested);

    // 倍速选择
    connect(speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoToolBarWidget::onSpeedComboChanged);

    // 分辨率选择
    connect(resolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoToolBarWidget::onResolutionComboChanged);

    // 音量控制
    connect(volumeButton, &QPushButton::clicked, this, &VideoToolBarWidget::onVolumeButtonClicked);
    connect(volumePopup, &VolumePopup::volumeChanged, this, &VideoToolBarWidget::onVolumePopupChanged);
}

void VideoToolBarWidget::setTimeText(const QString &timeText)
{
    timeLabel->setText(timeText);
}

void VideoToolBarWidget::setCurrentTime(qint64 milliseconds)
{
    currentTime_ = milliseconds;
    updateTimeDisplay();
}

void VideoToolBarWidget::setDuration(qint64 milliseconds)
{
    duration_ = milliseconds;
    progressSlider->setDuration(milliseconds);
    updateTimeDisplay();
}

void VideoToolBarWidget::setPreviewImage(const QImage &image)
{
    progressSlider->setPreviewImage(image);
}

void VideoToolBarWidget::updateTimeDisplay()
{
    // 格式化时间
    auto formatTime = [](qint64 ms) -> QString {
        if (ms < 0) ms = 0;
        int totalSeconds = ms / 1000;
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;
        
        if (hours > 0) {
            return QString("%1:%2:%3")
                .arg(hours)
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
        } else {
            return QString("%1:%2")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
        }
    };

    QString currentTimeStr = formatTime(currentTime_);
    QString totalTimeStr = formatTime(duration_);

    timeLabel->setText(QString("%1 / %2").arg(currentTimeStr, totalTimeStr));

    // 更新进度条
    if (duration_ > 0) {
        int progress = static_cast<int>((static_cast<double>(currentTime_) / duration_) * 1000);
        progress = qBound(0, progress, 1000);
        progressSlider->blockSignals(true);
        progressSlider->setValue(progress);
        progressSlider->blockSignals(false);
    }
}

void VideoToolBarWidget::setProgress(int value)
{
    if (value < 0) value = 0;
    if (value > 1000) value = 1000;

    progressSlider->blockSignals(true);
    progressSlider->setValue(value);
    progressSlider->blockSignals(false);
}

int VideoToolBarWidget::progress() const
{
    return progressSlider->value();
}

void VideoToolBarWidget::setPlaying(bool playing)
{
    isPlaying_ = playing;
    updatePlayButtons();
}

bool VideoToolBarWidget::isPlaying() const
{
    return isPlaying_;
}

void VideoToolBarWidget::updatePlayButtons()
{
    // 更新播放/暂停按钮
    if (isPlaying_) {
        playPauseButton->setText("⏸");
        playPauseButton->setToolTip("暂停");
    } else {
        playPauseButton->setText("▶");
        playPauseButton->setToolTip("播放");
    }
}

void VideoToolBarWidget::setSpeed(float speed)
{
    // 查找最近的倍速选项 (0.5 ~ 2.0)
    QStringList speedTexts = {"0.5", "0.75", "1.0", "1.25", "1.5", "1.75", "2.0"};
    QString speedStr = QString::number(speed, 'f', 2);

    // 去掉末尾的0和小数点
    if (speedStr.endsWith(".00")) {
        speedStr = speedStr.left(speedStr.length() - 3);
    } else if (speedStr.endsWith("0")) {
        speedStr = speedStr.left(speedStr.length() - 1);
    }

    // 尝试匹配
    int index = -1;
    for (int i = 0; i < speedTexts.size(); ++i) {
        if (speedStr == speedTexts[i] ||
            qFuzzyCompare(speed, speedTexts[i].toFloat())) {
            index = i;
            break;
        }
    }

    if (index >= 0) {
        speedCombo->blockSignals(true);
        speedCombo->setCurrentIndex(index);
        speedCombo->blockSignals(false);
    }
}

float VideoToolBarWidget::currentSpeed() const
{
    QString speedText = speedCombo->currentText();
    speedText = speedText.left(speedText.length() - 1); // 去掉"x"
    return speedText.toFloat();
}

void VideoToolBarWidget::setResolution(const QString &resolution)
{
    int index = resolutionCombo->findText(resolution);
    if (index >= 0) {
        resolutionCombo->blockSignals(true);
        resolutionCombo->setCurrentIndex(index);
        resolutionCombo->blockSignals(false);
    }
}

QString VideoToolBarWidget::currentResolution() const
{
    return resolutionCombo->currentText();
}

// ============ 私有槽函数 ============

void VideoToolBarWidget::onPlayPauseClicked()
{
    isPlaying_ = !isPlaying_;
    setPlaying(isPlaying_);
    emit playPauseClicked();
}

void VideoToolBarWidget::onSpeedComboChanged(int index)
{
    QString speedText = speedCombo->itemText(index);
    speedText = speedText.left(speedText.length() - 1); // 去掉"x"
    float speed = speedText.toFloat();
    emit speedChanged(speed);
}

void VideoToolBarWidget::onResolutionComboChanged(int index)
{
    QString resolution = resolutionCombo->itemText(index);
    emit resolutionChanged(resolution);
}

void VideoToolBarWidget::onProgressSliderChanged(int value)
{
    emit progressChanged(value);
}

void VideoToolBarWidget::onProgressSliderPressed()
{
    emit progressPressed();
}

void VideoToolBarWidget::onProgressSliderReleased()
{
    emit progressReleased();
}

void VideoToolBarWidget::onVolumeButtonClicked()
{
    if (volumePopup->isVisible()) {
        volumePopup->hide();
    } else {
        // 计算弹出位置（在音量按钮上方）
        QPoint buttonPos = volumeButton->mapToGlobal(QPoint(0, 0));
        int popupX = buttonPos.x() + (volumeButton->width() - volumePopup->width()) / 2;
        int popupY = buttonPos.y() - volumePopup->height() - 5;
        
        volumePopup->move(popupX, popupY);
        volumePopup->show();
    }
}

void VideoToolBarWidget::onVolumePopupChanged(int volume)
{
    volume_ = volume;
    muted_ = (volume == 0);
    
    // 更新按钮图标
    if (muted_ || volume == 0) {
        volumeButton->setText("🔇");
    } else if (volume < 30) {
        volumeButton->setText("🔈");
    } else if (volume < 70) {
        volumeButton->setText("🔉");
    } else {
        volumeButton->setText("🔊");
    }
    
    emit volumeChanged(volume);
    if (muted_) {
        emit mutedChanged(true);
    }
}

void VideoToolBarWidget::setVolume(int volume)
{
    volume = qBound(0, volume, 100);
    volume_ = volume;
    volumePopup->setVolume(volume);
    
    // 更新按钮图标
    if (volume == 0) {
        volumeButton->setText("🔇");
    } else if (volume < 30) {
        volumeButton->setText("🔈");
    } else if (volume < 70) {
        volumeButton->setText("🔉");
    } else {
        volumeButton->setText("🔊");
    }
}

int VideoToolBarWidget::currentVolume() const
{
    return volume_;
}

void VideoToolBarWidget::setMuted(bool muted)
{
    muted_ = muted;
    if (muted) {
        volumeButton->setText("🔇");
        volumePopup->setVolume(0);
    } else {
        setVolume(volume_ > 0 ? volume_ : 100);
    }
}

bool VideoToolBarWidget::isMuted() const
{
    return muted_;
}
