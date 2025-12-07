#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <thread>
#include <atomic>

// Forward declaration
typedef struct MediaPlayer MediaPlayer;

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
    
    // 状态查询
    int getState() const;
    long getCurrentPosition() const;
    long getDuration() const;

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    // 错误信号
    void errorOccurred(const QString &error);
    
    // 状态信号
    void stateChanged(int state);
    void prepared();
    void completed();
    
    // 播放进度信号
    void positionChanged(long position);
    void durationChanged(long duration);
    
    // 视频信号
    void videoSizeChanged(int width, int height);
    
    // 缓冲信号
    void bufferingStart();
    void bufferingEnd();
    void bufferingUpdate(int percent);
    
    // Seek 信号
    void seekComplete(long position);

private slots:
    // 消息处理槽（通过 Qt::QueuedConnection 从消息线程调用）
    void onMessage(int what, int arg1, int arg2);

private:
    void initPlayer();
    void cleanupPlayer();
    
    // 消息循环线程
    void startMessageLoop();
    void stopMessageLoop();
    static void messageLoopThread(PlayerWidget *self);
    void messageLoop();

    MediaPlayer *m_mp;
    QString m_mediaPath;
    bool m_initialized;
    
    // 消息线程
    std::thread m_msgThread;
    std::atomic<bool> m_msgLoopRunning;
    int m_lastState;
};

#endif // PLAYERWIDGET_H
