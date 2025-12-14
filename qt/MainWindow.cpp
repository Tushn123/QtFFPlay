#include "MainWindow.h"

#include <QToolBar>
#include <QFile>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow{parent}
{
    initUI();
    initConnect();
}

void MainWindow::initUI()
{
    // 在主窗口的构造函数中直接创建
    QToolBar *mvToolBar = new QToolBar(this);
    addToolBar(Qt::TopToolBarArea, mvToolBar);

    // 设置工具栏属性
    mvToolBar->setIconSize(QSize(24, 24));
    mvToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    // 创建动作组，实现互斥选择
    QActionGroup *actionGroup = new QActionGroup(mvToolBar);
    actionGroup->setExclusive(true);

// 使用宏定义添加所有按钮
#define ADD_BUTTON(name, rows, cols, text, iconPath) \
    { \
            QAction *action = new QAction(mvToolBar); \
            if (QFile::exists(iconPath)) { \
                action->setIcon(QIcon(iconPath)); \
        } else { \
                action->setText(QString("%1x%2").arg(rows).arg(cols)); \
        } \
            action->setToolTip(QString("%1x%2 分屏").arg(rows).arg(cols)); \
            action->setCheckable(true); \
            actionGroup->addAction(action); \
            mvToolBar->addAction(action); \
        \
            /* 将行和列存储到 action 的 data 中 */ \
            action->setData(QVariant(QPoint(rows, cols))); \
    }

    // 添加所有按钮
    FOREACH_MV_STYLE(ADD_BUTTON)

#undef ADD_BUTTON

    // 连接所有动作的信号
    for (QAction *action : mvToolBar->actions()) {
        connect(action, &QAction::triggered, this, [this, action]() {
            QPoint point = action->data().toPoint();
            multiVideoWidget->setLayout(point.x(), point.y());
        });
    }

    // 默认选中第一个按钮
    if (!mvToolBar->actions().isEmpty()) {
        mvToolBar->actions().first()->setChecked(true);
    }

    multiVideoWidget = new MultiVideoWidget(this);

    // 设置中心控件
    setCentralWidget(multiVideoWidget);
}

void MainWindow::initConnect(){

}
