#ifndef MULTIVIDEOWIDGET_H
#define MULTIVIDEOWIDGET_H

#include <QLabel>
#include <QWidget>
#include "htable.h"
#include "VideoWidget.h"

#define MV_STYLE_MAXNUM     4

// F(id, row, col, label, image)
#define FOREACH_MV_STYLE(F) \
F(MV_STYLE_1,  1, 1, " MV1",  ":/image/style1.png")     \
    F(MV_STYLE_2,  1, 2, " MV2",  ":/image/style2.png")     \
    F(MV_STYLE_4,  2, 2, " MV4",  ":/image/style4.png")     \
    F(MV_STYLE_9,  3, 3, " MV9",  ":/image/style9.png")     \
    F(MV_STYLE_16, 4, 4, " MV16", ":/image/style16.png")    \
    F(MV_STYLE_25, 5, 5, " MV25", ":/image/style25.png")    \
    F(MV_STYLE_36, 6, 6, " MV36", ":/image/style36.png")    \
    F(MV_STYLE_49, 7, 7, " MV49", ":/image/style49.png")    \
    F(MV_STYLE_64, 8, 8, " MV64",  ":/image/style64.png")   \

enum MV_STYLE {
#define ENUM_MV_STYLE(id, row, col, label, image) id,
    FOREACH_MV_STYLE(ENUM_MV_STYLE)
};

class MultiVideoWidget : public QWidget
{
    Q_OBJECT
public:
    enum Action {
        STRETCH,
        EXCHANGE,
        MERGE,
    };

    explicit MultiVideoWidget(QWidget *parent = nullptr);
    VideoWidget* getPlayerByID(int playerid);
    VideoWidget* getPlayerByPos(QPoint pt);
    VideoWidget* getIdlePlayer();

signals:

public slots:
    void setLayout(int row, int col);
    void saveLayout();
    void restoreLayout();
    void exchangeCells(VideoWidget* player1, VideoWidget* player2);
    void stretch(QWidget* wdg);

protected:
    void initUI();
    void initConnect();
    void updateUI();
    QRect adjustRect(QPoint pt1, QPoint pt2);
    qint64 getTickCount();

    virtual void resizeEvent(QResizeEvent* e);
    virtual void mouseDoubleClickEvent(QMouseEvent *e);

public:
    HTable table;
    HTable prev_table;
    QVector<QWidget*> views;
    QLabel *labRect;

    QPoint ptMousePress;
    qint64 tsMousePress;
    Action action;
    bool bStretch;
};

#endif // MULTIVIDEOWIDGET_H
