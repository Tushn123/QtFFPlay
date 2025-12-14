#include "MultiVideoWidget.h"
#include "qtstyles.h"

#include <QElapsedTimer>
#include <QResizeEvent>

MultiVideoWidget::MultiVideoWidget(QWidget *parent)
    : QWidget{parent}
{
    initUI();
    initConnect();
    bStretch = false;
}

void MultiVideoWidget::initUI() {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setLayout(2,2);

    for (int i = 0; i < MV_STYLE_MAXNUM; ++i) {
        VideoWidget* player = new VideoWidget(this);
        player->setId(i+1);
        views.push_back(player);
    }

    labRect = new QLabel(this);
    labRect->hide();
    labRect->setStyleSheet(RECT_QSS);
}

void MultiVideoWidget::initConnect() {

}

void MultiVideoWidget::setLayout(int row, int col) {
    saveLayout();
    table.init(row,col);
    updateUI();
}

void MultiVideoWidget::exchangeCells(VideoWidget* player1, VideoWidget* player2) {
    qDebug("exchange %d<=>%d", player1->getId(), player2->getId());

    QRect rcTmp = player1->geometry();
    int idTmp = player1->getId();

    player1->setGeometry(player2->geometry());
    player1->setId(player2->getId());

    player2->setGeometry(rcTmp);
    player2->setId(idTmp);
}

VideoWidget* MultiVideoWidget::getPlayerByID(int playerid) {
    for (int i = 0; i < views.size(); ++i) {
        VideoWidget *player = (VideoWidget*)views[i];
        if (player->getId() == playerid) {
            return player;
        }
    }
    return NULL;
}

VideoWidget* MultiVideoWidget::getPlayerByPos(QPoint pt) {
    for (int i = 0; i < views.size(); ++i) {
        QWidget* wdg = views[i];
        if (wdg->isVisible() && wdg->geometry().contains(pt)) {
            return (VideoWidget*)wdg;
        }
    }
    return NULL;
}

VideoWidget* MultiVideoWidget::getIdlePlayer() {
    // for (int i = 0; i < views.size(); ++i) {
    //     VideoWidget *player = (VideoWidget*)views[i];
    //     if (player->isVisible() && player->status == VideoWidget::STOP) {
    //         return player;
    //     }
    // }
    return NULL;
}

#define SEPARATOR_LINE_WIDTH 1
void MultiVideoWidget::updateUI() {
    int row = table.row;
    int col = table.col;
    if (row == 0 || col == 0) return;
    int cell_w = width()/col;
    int cell_h = height()/row;

    int margin_x = (width() - cell_w * col) / 2;
    int margin_y = (height() - cell_h * row) / 2;
    int x = margin_x;
    int y = margin_y;
    for (int i = 0; i < views.size(); ++i) {
        views[i]->hide();
    }

    int cnt = 0;
    HTableCell cell;
    for (int r = 0; r < row; ++r) {
        for (int c = 0; c < col; ++c) {
            int id = r*col + c + 1;
            if (table.getTableCell(id, cell)) {
                QWidget *wdg = getPlayerByID(id);
                if (wdg) {
                    wdg->setGeometry(x, y, cell_w*cell.colspan() - SEPARATOR_LINE_WIDTH, cell_h*cell.rowspan() - SEPARATOR_LINE_WIDTH);
                    wdg->show();
                    ++cnt;
                }
            }
            x += cell_w;
        }
        x = margin_x;
        y += cell_h;
    }

    bStretch = (cnt == 1);
}

void MultiVideoWidget::resizeEvent(QResizeEvent* e) {
    updateUI();
}

void MultiVideoWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    VideoWidget* player = getPlayerByPos(e->pos());
    if (player) {
        stretch(player);
    }
}

void MultiVideoWidget::stretch(QWidget* wdg) {
    if (table.row == 1 && table.col == 1) return;
    if (bStretch) {
        restoreLayout();
        bStretch = false;
    }
    else {
        saveLayout();
        for (int i = 0; i < views.size(); ++i) {
            views[i]->hide();
        }
        wdg->setGeometry(rect());
        wdg->show();
        bStretch = true;
    }
}

void MultiVideoWidget::saveLayout() {
    prev_table = table;
}

void MultiVideoWidget::restoreLayout() {
    HTable tmp = table;
    table = prev_table;
    prev_table = tmp;
    updateUI();
}

QRect MultiVideoWidget::adjustRect(QPoint pt1, QPoint pt2) {
    int x1 = qMin(pt1.x(), pt2.x());
    int x2 = qMax(pt1.x(), pt2.x());
    int y1 = qMin(pt1.y(), pt2.y());
    int y2 = qMax(pt1.y(), pt2.y());
    return QRect(QPoint(x1,y1), QPoint(x2,y2));
}

qint64 MultiVideoWidget::getTickCount() {
    static QElapsedTimer timer;
    static bool initialized = false;

    if (!initialized) {
        timer.start();
        initialized = true;
    }

    return timer.elapsed();
}

// void MultiVideoWidget::play(HMedia& media) {
//     VideoWidget* player = getIdlePlayer();
//     if (player == NULL) {
//         QMessageBox::information(this, tr("Info"), tr("No spare player, please stop one and try agian!"));
//     }
//     else {
//         player->open(media);
//         player->setPlaybackSpeed(2);
//     }
// }
