#include "VideoToolBarWidget.h"

#include <QVBoxLayout>

VideoToolBarWidget::VideoToolBarWidget(QWidget *parent)
    : QWidget{parent}
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
    timeLabel->setFixedWidth(100);

    // 进度条
    progressSlider = new QSlider(Qt::Horizontal, this);
    progressSlider->setRange(0, 1000);
    progressSlider->setValue(0);
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

    // 逐帧倒放按钮
    stepBackwardButton = new QPushButton("<<", this);
    stepBackwardButton->setFixedSize(30, 30);
    stepBackwardButton->setToolTip("逐帧倒放");
    stepBackwardButton->setStyleSheet(R"(
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

    // 倒放按钮
    backwardButton = new QPushButton("|<", this);
    backwardButton->setFixedSize(30, 30);
    backwardButton->setToolTip("倒放");
    backwardButton->setStyleSheet(R"(
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

    // 正放按钮
    forwardButton = new QPushButton(">|", this);
    forwardButton->setFixedSize(30, 30);
    forwardButton->setToolTip("正放");
    forwardButton->setStyleSheet(R"(
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

    // 添加左侧控件
    leftControlLayout->addWidget(stepBackwardButton);
    leftControlLayout->addWidget(backwardButton);
    leftControlLayout->addWidget(playPauseButton);
    leftControlLayout->addWidget(forwardButton);
    leftControlLayout->addWidget(stepForwardButton);

    // 中间：弹簧，实现两端对齐
    QSpacerItem *horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    // 右侧：倍速和分辨率选择区域
    QHBoxLayout *rightControlLayout = new QHBoxLayout();
    rightControlLayout->setSpacing(10);

    speedCombo = new QComboBox(this);
    speedCombo->setFixedWidth(80);
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

    // 添加倍速选项
    QStringList speedOptions = {"0.5x", "0.75x", "1.0x", "1.5x", "2.0x", "3.0x"};
    for (const QString &option : speedOptions) {
        speedCombo->addItem(option);
    }
    // 默认选择1.0x
    speedCombo->setCurrentIndex(2);

    resolutionCombo = new QComboBox(this);
    resolutionCombo->setFixedWidth(100);
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
    this->setFixedHeight(80);

}

void VideoToolBarWidget::initConnect()
{
    // 播放控制按钮
    connect(stepBackwardButton, &QPushButton::clicked, this, &VideoToolBarWidget::stepBackwardClicked);
    connect(backwardButton, &QPushButton::clicked, this, &VideoToolBarWidget::backwardClicked);
    connect(playPauseButton, &QPushButton::clicked, this, &VideoToolBarWidget::onPlayPauseClicked);
    connect(forwardButton, &QPushButton::clicked, this, &VideoToolBarWidget::forwardClicked);
    connect(stepForwardButton, &QPushButton::clicked, this, &VideoToolBarWidget::stepForwardClicked);

    // 进度条
    connect(progressSlider, &QSlider::valueChanged, this, &VideoToolBarWidget::onProgressSliderChanged);
    connect(progressSlider, &QSlider::sliderPressed, this, &VideoToolBarWidget::onProgressSliderPressed);
    connect(progressSlider, &QSlider::sliderReleased, this, &VideoToolBarWidget::onProgressSliderReleased);

    // 倍速选择
    connect(speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoToolBarWidget::onSpeedComboChanged);

    // 分辨率选择
    connect(resolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoToolBarWidget::onResolutionComboChanged);
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
    updateTimeDisplay();
}

void VideoToolBarWidget::updateTimeDisplay()
{
    // 格式化为 mm:ss
    int currentSeconds = currentTime_ / 1000;
    int totalSeconds = duration_ / 1000;

    QString currentTimeStr = QString("%1:%2")
                                 .arg(currentSeconds / 60, 2, 10, QChar('0'))
                                 .arg(currentSeconds % 60, 2, 10, QChar('0'));

    QString totalTimeStr = QString("%1:%2")
                               .arg(totalSeconds / 60, 2, 10, QChar('0'))
                               .arg(totalSeconds % 60, 2, 10, QChar('0'));

    timeLabel->setText(QString("%1 / %2").arg(currentTimeStr).arg(totalTimeStr));

    // 更新进度条
    if (duration_ > 0) {
        int progress = static_cast<int>((static_cast<double>(currentTime_) / duration_) * 1000);
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
    if (playing) {
        playPauseButton->setText("⏸");
        playPauseButton->setToolTip("暂停");
    } else {
        playPauseButton->setText("▶");
        playPauseButton->setToolTip("播放");
    }
}

bool VideoToolBarWidget::isPlaying() const
{
    return isPlaying_;
}

void VideoToolBarWidget::setSpeed(float speed)
{
    // 查找最近的倍速选项
    QStringList speedTexts = {"0.5", "0.75", "1.0", "1.5", "2.0", "3.0"};
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
