#include "VideoTitleBarWidget.h"

VideoTitleBarWidget::VideoTitleBarWidget(QWidget *parent)
    : QWidget{parent}
{
    this->setAttribute(Qt::WA_StyledBackground, true);
    this->setAutoFillBackground(true);
    this->setStyleSheet(
        "VideoTitleBarWidget {"
        "  background: qlineargradient("
        "    x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 rgba(0, 0, 0, 220),"  // 顶部的黑色
        "    stop:1 rgba(0, 0, 0, 180)"   // 底部的稍透明的黑色
        "  );"
        "}"
        );
}
