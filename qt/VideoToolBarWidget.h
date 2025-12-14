#ifndef VIDEOTOOLBARWIDGET_H
#define VIDEOTOOLBARWIDGET_H

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

class VideoToolBarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoToolBarWidget(QWidget *parent = nullptr);

    // 设置时间显示
    void setTimeText(const QString &timeText);
    void setCurrentTime(qint64 milliseconds);
    void setDuration(qint64 milliseconds);

    // 设置进度
    void setProgress(int value);
    int progress() const;

    // 设置播放状态
    void setPlaying(bool playing);
    bool isPlaying() const;

    // 设置倍速
    void setSpeed(float speed);
    float currentSpeed() const;

    // 设置分辨率
    void setResolution(const QString &resolution);
    QString currentResolution() const;

    void updateTimeDisplay();

protected:
    void initUI();
    void initConnect();

signals:
    // 播放控制信号
    void stepBackwardClicked();      // 逐帧倒放
    void backwardClicked();          // 倒放
    void playPauseClicked();         // 播放/暂停
    void stepForwardClicked();       // 逐帧正放
    void forwardClicked();           // 正放

    // 进度条信号
    void progressChanged(int value); // 进度条值改变
    void progressPressed();          // 进度条按下
    void progressReleased();         // 进度条释放

    // 倍速信号
    void speedChanged(float speed);  // 倍速改变

    // 分辨率信号
    void resolutionChanged(const QString &resolution); // 分辨率改变

private slots:
    void onPlayPauseClicked();
    void onSpeedComboChanged(int index);
    void onResolutionComboChanged(int index);
    void onProgressSliderChanged(int value);
    void onProgressSliderPressed();
    void onProgressSliderReleased();

public:
    // 上方区域控件
    QLabel *timeLabel;
    QSlider *progressSlider;

    // 下方区域左侧控件
    QPushButton *stepBackwardButton;
    QPushButton *backwardButton;
    QPushButton *playPauseButton;
    QPushButton *forwardButton;
    QPushButton *stepForwardButton;

    // 下方区域右侧控件
    QComboBox *speedCombo;
    QComboBox *resolutionCombo;

    // 状态
    bool isPlaying_;
    qint64 currentTime_;
    qint64 duration_;
};

#endif // VIDEOTOOLBARWIDGET_H
