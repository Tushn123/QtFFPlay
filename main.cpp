/*
 * Copyright (c) 2024 FFPlayer contributors
 *
 * Qt 播放器入口
 */

#include <QApplication>
#include <cstdio>
#include "PlayerWidget.h"

/* 全局变量（cmdutils/opt_common 需要） */
extern "C" const char program_name[] = "qt_ffplay";
extern "C" const int program_birth_year = 2025;

int main(int argc, char *argv[])
{
    printf("Starting Qt FFPlayer...\n");
    fflush(stdout);
    
    QApplication app(argc, argv);
    
    printf("QApplication created\n");
    fflush(stdout);
    
    // 创建播放器窗口
    PlayerWidget player;
    player.setWindowTitle("Qt FFPlayer");
    player.resize(960, 540);
    player.show();
    
    // 设置默认媒体文件
    QString mediaPath = "C:/shn/media/animal.mp4";
    
    // 如果有命令行参数，使用第一个参数作为媒体路径
    if (argc > 1) {
        mediaPath = QString::fromUtf8(argv[1]);
    }
    
    // 设置媒体
    player.setMedia(mediaPath);
    
    // 开始播放
    player.play();
    
    return app.exec();
}

