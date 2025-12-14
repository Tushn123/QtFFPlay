/*
 * Copyright (c) 2024 FFPlayer contributors
 *
 * Qt 播放器入口
 */

#include <QApplication>
#include <cstdio>
#include <qt/MainWindow.h>
#include "qt/PlayerWidget.h"
#include "qt/MultiVideoWidget.h"

/* 全局变量（cmdutils/opt_common 需要） */
extern "C" const char program_name[] = "qt_ffplay";
extern "C" const int program_birth_year = 2025;

int main(int argc, char *argv[])
{    
    QApplication app(argc, argv);
    // MultiVideoWidget multiVideoWidget;
    // multiVideoWidget.show();
    MainWindow mainWindow;
    mainWindow.setMinimumSize(800,600);
    mainWindow.show();

    // VideoWidget videoWidget;
    // videoWidget.show();
    // VideoWidget videoWidget2;
    // videoWidget2.show();
    return app.exec();
}

