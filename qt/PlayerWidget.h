#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QPoint>
#include <QTimer>
#include <thread>
#include <atomic>

// Forward declarations
typedef struct MediaPlayer MediaPlayer;
struct FFPVideoFrame;
class VideoGLWidget;

/**
 * 渲染模式
 */
enum class RenderMode {
    SDL,      // SDL 子窗口渲染
    OpenGL    // Qt OpenGL 渲染（默认）
};

class PlayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerWidget(QWidget *parent = nullptr);
    ~PlayerWidget();

    /**
     * 设置渲染模式（必须在 setMedia 之前调用）
     * @param mode 渲染模式
     */
    void setRenderMode(RenderMode mode);
    RenderMode renderMode() const { return m_renderMode; }

    void setMedia(const QString &path);
    void play();
    void pause();
    void stop();
    void togglePause();
    
    /**
     * 逐帧前进
     * 如果正在播放，先暂停再前进一帧
     */
    void stepForward();
    
    /**
     * 跳转到指定位置
     * @param msec 目标位置（毫秒）
     */
    void seekTo(long msec);
    
    bool isPlaying() const;
    bool isPaused() const;
    
    // 状态查询
    int getState() const;
    long getCurrentPosition() const;
    long getDuration() const;
    
    // 播放速率
    void setPlaybackRate(float rate);
    float playbackRate() const;
    
    // 音量控制
    void setVolume(int volume);  // 0-100
    int volume() const;

    // 获取 OpenGL 渲染组件
    VideoGLWidget* videoWidget() const { return m_videoWidget; }

    /**
     * 硬件加速类型
     */
    enum class HWAccelType {
        None = 0,          // 软解码
        Auto,              // 自动选择
        DXVA2,             // Windows DXVA2
        D3D11VA,           // Windows D3D11
        CUDA,              // NVIDIA CUDA
        VAAPI,             // Linux VAAPI
        VDPAU,             // Linux VDPAU
        VideoToolbox,      // macOS VideoToolbox
        QSV                // Intel Quick Sync
    };
    Q_ENUM(HWAccelType)

    /**
     * 设置硬件加速类型（必须在 setMedia 之前调用）
     * @param type 硬件加速类型
     */
    void setHWAccelType(HWAccelType type);
    
    /**
     * 获取当前硬件加速类型
     */
    HWAccelType hwAccelType() const;
    
    /**
     * 获取可用的硬件加速列表
     * @return 硬件加速类型列表 (type, name, available)
     */
    static QList<std::tuple<HWAccelType, QString, bool>> availableHWAccels();
    
    /**
     * 检查指定硬件加速是否可用
     */
    static bool isHWAccelAvailable(HWAccelType type);
    
    /**
     * 获取硬件加速类型名称
     */
    static QString hwAccelName(HWAccelType type);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

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
    
    // 定时更新播放位置
    void updatePosition();

private:
    void initPlayer();
    void cleanupPlayer();
    void setupLayout();
    
    // 视频帧回调（静态，供 C 层调用）
    static void videoFrameCallback(void *opaque, FFPVideoFrame *frame);
    
    // 消息循环线程（ijkplayer 风格：上层驱动消息循环）
    void startMessageLoop();
    void stopMessageLoop();
    static void messageLoopThread(PlayerWidget *self);
    void messageLoop();

    MediaPlayer *m_mp;
    QString m_mediaPath;
    bool m_initialized;
    
    // 渲染模式
    RenderMode m_renderMode;
    
    // 硬件加速类型
    HWAccelType m_hwAccelType;
    
    // OpenGL 渲染组件
    VideoGLWidget *m_videoWidget;
    QVBoxLayout *m_layout;
    
    // 消息线程
    std::thread m_msgThread;
    std::atomic<bool> m_msgLoopRunning;
    int m_lastState;
    
    // 播放标志：如果在准备阶段调用 play()，准备完成后自动播放
    bool m_startOnPrepared;
    
    // 播放位置更新定时器
    QTimer *m_positionTimer;
    long m_lastPosition;
    
    // ========== 画面控制交互状态 ==========
    bool m_spacePressed;        // 空格键是否按下（用于拖动）
    bool m_isPanning;           // 是否正在拖动画面
    QPoint m_lastMousePos;      // 上次鼠标位置
    
    // 缩放步进
    static constexpr float ZOOM_STEP = 0.1f;
};

#endif // PLAYERWIDGET_H
