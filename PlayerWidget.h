#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>

// Forward declaration
typedef struct FFPlayer FFPlayer;

class PlayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerWidget(QWidget *parent = nullptr);
    ~PlayerWidget();

    void setMedia(const QString &path);
    void play();
    void pause();
    void stop();
    void togglePause();
    
    bool isPlaying() const;
    bool isPaused() const;

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    void errorOccurred(const QString &error);

private:
    void initPlayer();
    void cleanupPlayer();

    FFPlayer *m_ffp;
    QString m_mediaPath;
    bool m_initialized;
};

#endif // PLAYERWIDGET_H

