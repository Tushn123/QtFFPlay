#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include "PlayerWidget.h"
#include "VideoTitleBarWidget.h"
#include "VideoToolBarWidget.h"

class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);

    void initUi();

    int getId() const;
    void setId(int newId);
    void updateBarPosition();
    
    /**
     * 完全停止播放并重置所有控件状态
     * 包括：停止播放器、清空画面、重置工具栏进度/时长/状态
     */
    void stopAndReset();

signals:

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

public:
    PlayerWidget *playerWidget = nullptr;
    VideoTitleBarWidget *videoTitleBarWidget = nullptr;
    VideoToolBarWidget *videoToolBarWidget = nullptr;

private:
    int id;     // 在MultiVideoWidget中的序号
};

#endif // VIDEOWIDGET_H
