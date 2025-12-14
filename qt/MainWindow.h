#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include "MultiVideoWidget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void initUI();
    void initConnect();

    MultiVideoWidget *multiVideoWidget = nullptr;

signals:
};

#endif // MAINWINDOW_H
