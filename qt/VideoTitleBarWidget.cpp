#include "VideoTitleBarWidget.h"
#include <QFileDialog>
#include <QStandardPaths>
#include <QFileInfo>
#include <QMouseEvent>
#include <QKeyEvent>

// ============ EditablePathLabel 实现 ============

EditablePathLabel::EditablePathLabel(QWidget *parent)
    : QLineEdit(parent)
    , m_editing(false)
{
    setReadOnly(true);
    setFrame(false);
    setCursor(Qt::ArrowCursor);
    
    // 默认只读样式
    setStyleSheet(R"(
        QLineEdit {
            background-color: transparent;
            color: #cccccc;
            font-size: 13px;
            border: none;
            padding: 2px 4px;
        }
        QLineEdit:hover {
            color: white;
        }
    )");
    
    setText("双击输入视频路径...");
    setToolTip("双击编辑路径，回车确认播放");
}

void EditablePathLabel::setPath(const QString &path)
{
    m_currentPath = path;
    updateDisplayText();
}

void EditablePathLabel::updateDisplayText()
{
    if (m_editing) {
        // 编辑模式显示完整路径
        setText(m_currentPath);
    } else {
        // 只读模式显示文件名
        if (m_currentPath.isEmpty()) {
            setText("双击输入视频路径...");
        } else {
            QFileInfo fi(m_currentPath);
            setText(fi.fileName());
            setToolTip(m_currentPath + "\n双击编辑路径");
        }
    }
}

void EditablePathLabel::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    enterEditMode();
}

void EditablePathLabel::enterEditMode()
{
    if (m_editing) return;
    
    m_editing = true;
    setReadOnly(false);
    setCursor(Qt::IBeamCursor);
    
    // 编辑模式样式
    setStyleSheet(R"(
        QLineEdit {
            background-color: #2a2a2a;
            color: white;
            font-size: 13px;
            border: 1px solid #0078d4;
            border-radius: 3px;
            padding: 2px 4px;
            selection-background-color: #0078d4;
        }
    )");
    
    setText(m_currentPath);
    selectAll();
    setFocus();
}

void EditablePathLabel::exitEditMode(bool confirm)
{
    if (!m_editing) return;
    
    m_editing = false;
    setReadOnly(true);
    setCursor(Qt::ArrowCursor);
    
    // 恢复只读样式
    setStyleSheet(R"(
        QLineEdit {
            background-color: transparent;
            color: #cccccc;
            font-size: 13px;
            border: none;
            padding: 2px 4px;
        }
        QLineEdit:hover {
            color: white;
        }
    )");
    
    if (confirm) {
        QString newPath = text().trimmed();
        if (!newPath.isEmpty() && newPath != m_currentPath) {
            // 路径变化，发出信号
            m_currentPath = newPath;
            emit pathChanged(newPath);
        }
    }
    
    updateDisplayText();
    clearFocus();
}

void EditablePathLabel::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    if (m_editing) {
        exitEditMode(true);  // 失去焦点时确认
    }
}

void EditablePathLabel::keyPressEvent(QKeyEvent *event)
{
    if (m_editing) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            exitEditMode(true);  // 回车确认
            return;
        } else if (event->key() == Qt::Key_Escape) {
            exitEditMode(false);  // Esc 取消
            return;
        }
    }
    QLineEdit::keyPressEvent(event);
}

void EditablePathLabel::onEditingFinished()
{
    // 由 focusOutEvent 和 keyPressEvent 处理
}

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
    
    // 使用 QPalette 设置半透明深灰背景
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(19, 19, 19, 220));  // 深灰色，带透明度
    setPalette(pal);
    
    initUI();
    initConnect();
}

void VideoTitleBarWidget::initUI()
{
    // 主布局
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 5, 10, 5);
    mainLayout->setSpacing(10);
    
    // 左侧：可编辑路径标签
    titleEdit = new EditablePathLabel(this);
    titleEdit->setMinimumWidth(150);
    titleEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    // 直播指示器（默认隐藏）
    liveIndicator = new QLabel("● LIVE", this);
    liveIndicator->setStyleSheet(R"(
        QLabel {
            color: #ff4444;
            font-size: 13px;
            font-weight: bold;
            padding: 2px 8px;
            background-color: rgba(255, 68, 68, 50);
            border-radius: 4px;
        }
    )");
    liveIndicator->hide();
    
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
    mainLayout->addWidget(titleEdit, 1);  // 路径占用剩余空间
    mainLayout->addWidget(liveIndicator);
    mainLayout->addWidget(openButton);
    mainLayout->addWidget(scaleModeCombo);
}

void VideoTitleBarWidget::initConnect()
{
    connect(scaleModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoTitleBarWidget::onScaleModeComboChanged);
    connect(openButton, &QPushButton::clicked,
            this, &VideoTitleBarWidget::onOpenButtonClicked);
    connect(titleEdit, &EditablePathLabel::pathChanged,
            this, &VideoTitleBarWidget::onPathEdited);
}

void VideoTitleBarWidget::onScaleModeComboChanged(int index)
{
    ScaleMode mode = static_cast<ScaleMode>(scaleModeCombo->itemData(index).toInt());
    emit scaleModeChanged(mode);
}

void VideoTitleBarWidget::onOpenButtonClicked()
{
    // 获取默认目录（当前路径的目录或视频文件夹）
    QString currentPath = titleEdit->path();
    QString defaultDir;
    if (!currentPath.isEmpty()) {
        QFileInfo fi(currentPath);
        defaultDir = fi.absolutePath();
    }
    if (defaultDir.isEmpty() || !QFileInfo::exists(defaultDir)) {
        defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    }
    
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
        titleEdit->setPath(filePath);
        emit openFileRequested(filePath);
    }
}

void VideoTitleBarWidget::onPathEdited(const QString &newPath)
{
    // 用户手动编辑了路径，发出打开文件信号
    if (!newPath.isEmpty()) {
        emit openFileRequested(newPath);
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

void VideoTitleBarWidget::setVideoPath(const QString &path)
{
    titleEdit->setPath(path);
}

QString VideoTitleBarWidget::videoPath() const
{
    return titleEdit->path();
}

void VideoTitleBarWidget::setLiveMode(bool isLive)
{
    if (isLive) {
        liveIndicator->show();
        qDebug() << "[VideoTitleBarWidget] Live mode enabled";
    } else {
        liveIndicator->hide();
        qDebug() << "[VideoTitleBarWidget] Live mode disabled";
    }
}
