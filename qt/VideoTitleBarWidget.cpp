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
    
    // 硬件加速下拉框
    hwAccelCombo = new QComboBox(this);
    hwAccelCombo->setFixedWidth(100);
    hwAccelCombo->setToolTip("选择解码方式（需重新加载视频生效）");
    hwAccelCombo->setStyleSheet(comboBoxStyle);
    populateHWAccelCombo();
    
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
    mainLayout->addWidget(hwAccelCombo);
    mainLayout->addWidget(scaleModeCombo);
}

void VideoTitleBarWidget::populateHWAccelCombo()
{
    hwAccelCombo->clear();
    
    // 获取可用的硬件加速列表
    auto hwAccels = PlayerWidget::availableHWAccels();
    
    for (const auto &item : hwAccels) {
        PlayerWidget::HWAccelType type = std::get<0>(item);
        QString name = std::get<1>(item);
        bool available = std::get<2>(item);
        
        // 只添加可用的选项（软解码始终可用）
        if (available || type == PlayerWidget::HWAccelType::None) {
            QString displayName = name;
            if (!available && type != PlayerWidget::HWAccelType::None) {
                displayName += " (不可用)";
            }
            hwAccelCombo->addItem(displayName, static_cast<int>(type));
        }
    }
    
    // 默认选择软解码
    hwAccelCombo->setCurrentIndex(0);
}

void VideoTitleBarWidget::initConnect()
{
    connect(scaleModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoTitleBarWidget::onScaleModeComboChanged);
    connect(hwAccelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoTitleBarWidget::onHWAccelComboChanged);
}

void VideoTitleBarWidget::onScaleModeComboChanged(int index)
{
    ScaleMode mode = static_cast<ScaleMode>(scaleModeCombo->itemData(index).toInt());
    emit scaleModeChanged(mode);
}

void VideoTitleBarWidget::onHWAccelComboChanged(int index)
{
    PlayerWidget::HWAccelType type = static_cast<PlayerWidget::HWAccelType>(
        hwAccelCombo->itemData(index).toInt());
    emit hwAccelTypeChanged(type);
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

void VideoTitleBarWidget::setHWAccelType(PlayerWidget::HWAccelType type)
{
    int index = hwAccelCombo->findData(static_cast<int>(type));
    if (index >= 0) {
        hwAccelCombo->setCurrentIndex(index);
    }
}

PlayerWidget::HWAccelType VideoTitleBarWidget::hwAccelType() const
{
    int index = hwAccelCombo->currentIndex();
    return static_cast<PlayerWidget::HWAccelType>(hwAccelCombo->itemData(index).toInt());
}
