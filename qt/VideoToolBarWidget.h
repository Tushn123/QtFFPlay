#ifndef VIDEOTOOLBARWIDGET_H
#define VIDEOTOOLBARWIDGET_H

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>
#include <QVBoxLayout>
#include <QFrame>
#include <QImage>
#include <QTimer>

/**
 * 进度条预览浮窗（包含预览画面和时间）
 */
class ProgressTooltip : public QFrame
{
    Q_OBJECT
public:
    explicit ProgressTooltip(QWidget *parent = nullptr);
    
    void setTime(qint64 milliseconds);
    void setPreviewImage(const QImage &image);
    void clearPreview();
    void showAt(const QPoint &globalPos);
    
    // 预览图尺寸
    static constexpr int PREVIEW_WIDTH = 160;
    static constexpr int PREVIEW_HEIGHT = 90;
    
private:
    QLabel *m_previewLabel;  // 预览画面
    QLabel *m_timeLabel;     // 时间显示
};

/**
 * 自定义视频进度条
 * - 支持点击跳转
 * - 支持悬浮时间提示
 */
class VideoProgressSlider : public QSlider
{
    Q_OBJECT
public:
    explicit VideoProgressSlider(QWidget *parent = nullptr);
    ~VideoProgressSlider();
    
    void setDuration(qint64 milliseconds);
    qint64 duration() const { return m_duration; }
    
    // 设置预览图片（由外部调用）
    void setPreviewImage(const QImage &image);
    
signals:
    // 点击或拖动请求跳转到指定位置
    void seekRequested(qint64 position);
    // 请求指定位置的预览帧
    void previewRequested(qint64 position);
    
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    
private slots:
    void onPreviewDelayTimeout();
    
private:
    qint64 positionFromMouse(int x) const;
    int valueFromPosition(int x) const;
    void updateTooltip(int x);
    
    ProgressTooltip *m_tooltip;
    qint64 m_duration;
    bool m_isDragging;
    qint64 m_lastPreviewPos;  // 上次请求预览的位置，避免重复请求
    
    // 预览延迟（避免快速移动时频繁解码）
    QTimer *m_previewDelayTimer;
    qint64 m_pendingPreviewPos;  // 待请求的预览位置
    static constexpr int PREVIEW_DELAY_MS = 200;  // 延迟时间
};

/**
 * 音量弹出控件 - 垂直音量滑块
 */
class VolumePopup : public QFrame
{
    Q_OBJECT
public:
    explicit VolumePopup(QWidget *parent = nullptr);
    
    void setVolume(int volume);
    int volume() const;
    
signals:
    void volumeChanged(int volume);
    
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    
private:
    QSlider *m_slider;
    QLabel *m_label;
};

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

    // 播放方向枚举
    enum PlayDirection {
        Forward,    // 正放
        Backward    // 倒放
    };

    // 设置播放状态
    void setPlaying(bool playing);
    bool isPlaying() const;
    
    // 设置播放方向
    void setPlayDirection(PlayDirection direction);
    PlayDirection playDirection() const;

    // 设置倍速
    void setSpeed(float speed);
    float currentSpeed() const;

    // 设置分辨率
    void setResolution(const QString &resolution);
    QString currentResolution() const;

    // 设置音量
    void setVolume(int volume);
    int currentVolume() const;
    void setMuted(bool muted);
    bool isMuted() const;

    void updateTimeDisplay();
    
    // 设置预览图片（用于进度条悬停预览）
    void setPreviewImage(const QImage &image);

protected:
    void initUI();
    void initConnect();

signals:
    // 播放控制信号
    void stepBackwardClicked();      // 逐帧倒放
    void backwardClicked();          // 倒放
    void playPauseClicked();         // 播放/暂停切换
    void stepForwardClicked();       // 逐帧正放
    void forwardClicked();           // 正放
    void stopClicked();              // 停止（关闭视频释放资源）
    
    // 播放状态信号
    void playStateChanged(bool playing, PlayDirection direction);  // 播放状态改变
    void forwardPlay();              // 开始正放
    void forwardPause();             // 正放暂停
    void backwardPlay();             // 开始倒放
    void backwardPause();            // 倒放暂停

    // 进度条信号
    void progressChanged(int value); // 进度条值改变
    void progressPressed();          // 进度条按下
    void progressReleased();         // 进度条释放
    void seekRequested(qint64 position);    // 请求跳转到指定位置（毫秒）
    void previewRequested(qint64 position); // 请求指定位置的预览帧

    // 倍速信号
    void speedChanged(float speed);  // 倍速改变

    // 分辨率信号
    void resolutionChanged(const QString &resolution); // 分辨率改变

    // 音量信号
    void volumeChanged(int volume);    // 音量改变
    void mutedChanged(bool muted);     // 静音状态改变

private slots:
    void onPlayPauseClicked();
    void onForwardClicked();
    void onBackwardClicked();
    void onSpeedComboChanged(int index);
    void onResolutionComboChanged(int index);
    void onProgressSliderChanged(int value);
    void onProgressSliderPressed();
    void onProgressSliderReleased();
    void onVolumeButtonClicked();
    void onVolumePopupChanged(int volume);
    
private:
    void updatePlayButtons();  // 更新按钮状态显示

public:
    // 上方区域控件
    QLabel *timeLabel;
    VideoProgressSlider *progressSlider;

    // 下方区域左侧控件
    QPushButton *stepBackwardButton;
    QPushButton *backwardButton;
    QPushButton *playPauseButton;
    QPushButton *forwardButton;
    QPushButton *stepForwardButton;
    QPushButton *stopButton;

    // 下方区域右侧控件
    QPushButton *volumeButton;
    VolumePopup *volumePopup;
    QComboBox *speedCombo;
    QComboBox *resolutionCombo;

    // 状态
    bool isPlaying_;
    PlayDirection direction_;  // 当前播放方向
    qint64 currentTime_;
    qint64 duration_;
    int volume_;
    bool muted_;
};

#endif // VIDEOTOOLBARWIDGET_H
