#ifndef VIDEOTITLEBARWIDGET_H
#define VIDEOTITLEBARWIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include "VideoGLWidget.h"

class VideoTitleBarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoTitleBarWidget(QWidget *parent = nullptr);

    /**
     * 设置当前缩放模式
     * @param mode 缩放模式
     */
    void setScaleMode(ScaleMode mode);
    
    /**
     * 获取当前缩放模式
     */
    ScaleMode scaleMode() const;

signals:
    /**
     * 缩放模式改变信号
     */
    void scaleModeChanged(ScaleMode mode);
    
    /**
     * 打开文件信号
     * @param filePath 用户选择的文件路径
     */
    void openFileRequested(const QString &filePath);

private slots:
    void onScaleModeComboChanged(int index);
    void onOpenButtonClicked();

public:
    QPushButton *openButton;
    QComboBox *scaleModeCombo;

private:
    void initUI();
    void initConnect();

    QLabel *titleLabel;
};

#endif // VIDEOTITLEBARWIDGET_H
