#include "VideoTitleBarWidget.h"
#include <QFileDialog>
#include <QStandardPaths>

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
    
    // 打开文件按钮
    openButton = new QPushButton("📂 打开", this);
    openButton->setFixedSize(70, 26);
    openButton->setToolTip("打开视频文件");
    openButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0066cc;
            color: white;
            border: 1px solid #0055aa;
            border-radius: 4px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #0077dd;
        }
        QPushButton:pressed {
            background-color: #0055bb;
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
    mainLayout->addWidget(openButton);
    mainLayout->addStretch();
    mainLayout->addWidget(scaleModeCombo);
}

void VideoTitleBarWidget::initConnect()
{
    connect(scaleModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoTitleBarWidget::onScaleModeComboChanged);
    connect(openButton, &QPushButton::clicked,
            this, &VideoTitleBarWidget::onOpenButtonClicked);
}

void VideoTitleBarWidget::onScaleModeComboChanged(int index)
{
    ScaleMode mode = static_cast<ScaleMode>(scaleModeCombo->itemData(index).toInt());
    emit scaleModeChanged(mode);
}

void VideoTitleBarWidget::onOpenButtonClicked()
{
    // 获取默认目录（视频文件夹或上次打开的目录）
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    
    // 打开文件选择对话框
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "打开视频文件",
        defaultDir,
        "视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.ts *.m2ts);;"
        "所有文件 (*.*)"
    );
    
    // 如果用户选择了文件
    if (!filePath.isEmpty()) {
        emit openFileRequested(filePath);
    }
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
