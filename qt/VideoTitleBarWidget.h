#ifndef VIDEOTITLEBARWIDGET_H
#define VIDEOTITLEBARWIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QHBoxLayout>
#include "VideoGLWidget.h"
#include "PlayerWidget.h"

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

    /**
     * 设置当前硬件加速类型
     * @param type 硬件加速类型
     */
    void setHWAccelType(PlayerWidget::HWAccelType type);
    
    /**
     * 获取当前硬件加速类型
     */
    PlayerWidget::HWAccelType hwAccelType() const;

signals:
    /**
     * 缩放模式改变信号
     */
    void scaleModeChanged(ScaleMode mode);
    
    /**
     * 硬件加速类型改变信号
     */
    void hwAccelTypeChanged(PlayerWidget::HWAccelType type);

private slots:
    void onScaleModeComboChanged(int index);
    void onHWAccelComboChanged(int index);

public:
    QComboBox *scaleModeCombo;
    QComboBox *hwAccelCombo;

private:
    void initUI();
    void initConnect();
    void populateHWAccelCombo();

    QLabel *titleLabel;
};

#endif // VIDEOTITLEBARWIDGET_H
