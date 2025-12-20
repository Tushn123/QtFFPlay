#include "VideoTitleBarWidget.h"

// 下拉框通用样式
static const char* comboBoxStyle = R"(
    QComboBox {
        background-color: #333333;
        color: white;
        border: 1px solid #555555;
        border-radius: 4px;
        padding: 3px 8px;
        font-size: 12px;
    }
    QComboBox:hover {
        background-color: #444444;
        border-color: #666666;
    }
    QComboBox::drop-down {
        border: none;
        width: 20px;
    }
    QComboBox::down-arrow {
        image: none;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 6px solid #aaaaaa;
        margin-right: 5px;
    }
    QComboBox QAbstractItemView {
        background-color: #2a2a2a;
        color: white;
        selection-background-color: #0078d4;
        selection-color: white;
        border: 1px solid #555555;
        outline: none;
    }
    QComboBox QAbstractItemView::item {
        padding: 5px 10px;
        min-height: 25px;
    }
    QComboBox QAbstractItemView::item:hover {
        background-color: #3a3a3a;
    }
)";

VideoTitleBarWidget::VideoTitleBarWidget(QWidget *parent)
    : QWidget{parent}
{
    this->setAttribute(Qt::WA_StyledBackground, true);
    this->setAutoFillBackground(true);
    this->setStyleSheet(
        "VideoTitleBarWidget {"
        "  background: qlineargradient("
        "    x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 rgba(0, 0, 0, 220),"
        "    stop:1 rgba(0, 0, 0, 180)"
        "  );"
        "}"
    );
    
    initUI();
    initConnect();
}

void VideoTitleBarWidget::initUI()
{
    // 主布局
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 5, 10, 5);
    mainLayout->setSpacing(10);
    
    // 左侧：标题
    titleLabel = new QLabel("视频播放器", this);
    titleLabel->setStyleSheet(R"(
        QLabel {
            color: white;
            font-size: 14px;
            font-weight: bold;
        }
    )");
    
    // 缩放模式下拉框
    scaleModeCombo = new QComboBox(this);
    scaleModeCombo->setFixedWidth(90);
    scaleModeCombo->addItem("适应窗口", static_cast<int>(ScaleMode::Fit));
    scaleModeCombo->addItem("拉伸填充", static_cast<int>(ScaleMode::Stretch));
    scaleModeCombo->addItem("裁剪填充", static_cast<int>(ScaleMode::Fill));
    scaleModeCombo->setCurrentIndex(0);
    scaleModeCombo->setToolTip("选择画面缩放模式");
    scaleModeCombo->setStyleSheet(comboBoxStyle);
    
    // 添加到布局
    mainLayout->addWidget(titleLabel);
    mainLayout->addStretch();
    mainLayout->addWidget(scaleModeCombo);
}

void VideoTitleBarWidget::initConnect()
{
    connect(scaleModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoTitleBarWidget::onScaleModeComboChanged);
}

void VideoTitleBarWidget::onScaleModeComboChanged(int index)
{
    ScaleMode mode = static_cast<ScaleMode>(scaleModeCombo->itemData(index).toInt());
    emit scaleModeChanged(mode);
}

void VideoTitleBarWidget::setScaleMode(ScaleMode mode)
{
    int index = scaleModeCombo->findData(static_cast<int>(mode));
    if (index >= 0) {
        scaleModeCombo->setCurrentIndex(index);
    }
}

ScaleMode VideoTitleBarWidget::scaleMode() const
{
    int index = scaleModeCombo->currentIndex();
    return static_cast<ScaleMode>(scaleModeCombo->itemData(index).toInt());
}
