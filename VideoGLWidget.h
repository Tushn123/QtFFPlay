/*
 * VideoGLWidget.h
 * 
 * Qt OpenGL 视频渲染组件
 * 支持 YUV420P 格式的高效 GPU 渲染
 * 支持多种缩放模式、画面缩放和平移
 */

#ifndef VIDEOGLWIDGET_H
#define VIDEOGLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMutex>
#include <QPointF>

// Forward declaration
struct FFPVideoFrame;

/**
 * 画面缩放模式
 */
enum class ScaleMode {
    Fit,      // 保持比例，适应窗口（可能有黑边）
    Stretch,  // 拉伸填充窗口（可能变形）
    Fill      // 保持比例，裁剪填充（无黑边）
};

class VideoGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit VideoGLWidget(QWidget *parent = nullptr);
    ~VideoGLWidget();

    /**
     * 更新视频帧数据（线程安全）
     * 可从任意线程调用，数据会被复制
     * @param frame 视频帧数据
     */
    void updateFrame(const FFPVideoFrame *frame);

    /**
     * 清除显示内容
     */
    void clearFrame();

    /**
     * 获取视频原始尺寸
     */
    QSize videoSize() const { return m_videoSize; }

    // ========== 缩放模式控制 ==========
    
    /**
     * 设置画面缩放模式
     * @param mode 缩放模式
     */
    void setScaleMode(ScaleMode mode);
    
    /**
     * 获取当前缩放模式
     */
    ScaleMode scaleMode() const { return m_scaleMode; }

    // ========== 缩放和平移控制 ==========
    
    /**
     * 设置缩放因子
     * @param factor 缩放因子 (1.0 ~ 10.0)
     */
    void setZoom(float factor);
    
    /**
     * 获取当前缩放因子
     */
    float zoom() const { return m_zoomFactor; }
    
    /**
     * 设置平移偏移
     * @param offset 平移偏移（像素）
     */
    void setPan(const QPointF &offset);
    
    /**
     * 获取当前平移偏移
     */
    QPointF pan() const { return m_panOffset; }
    
    /**
     * 获取最大平移范围
     */
    QPointF maxPan() const;
    
    /**
     * 重置视图（缩放和平移恢复默认）
     */
    void resetView();
    
    /**
     * 以指定点为中心进行缩放
     * @param factor 新的缩放因子
     * @param center 缩放中心点（窗口坐标）
     */
    void zoomAt(float factor, const QPointF &center);

signals:
    /**
     * 视频尺寸变化信号
     */
    void videoSizeChanged(int width, int height);

    /**
     * 内部信号：请求更新帧（跨线程）
     */
    void frameReady();

    /**
     * 缩放因子变化信号
     */
    void zoomChanged(float factor);

    /**
     * 缩放模式变化信号
     */
    void scaleModeChanged(ScaleMode mode);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private slots:
    void onFrameReady();

private:
    void initShaders();
    void initGeometry();
    void initTextures();
    void updateTextures();
    void cleanupGL();

    /**
     * 计算视口矩形（考虑缩放模式、缩放因子和平移）
     * @param widgetW 窗口宽度
     * @param widgetH 窗口高度
     * @return 视口矩形 (x, y, width, height)
     */
    QRectF calculateViewport(int widgetW, int widgetH);

    /**
     * 限制平移范围
     */
    void clampPanOffset();

    // Shader program
    QOpenGLShaderProgram *m_program;

    // YUV 三个平面的纹理
    GLuint m_textureY;
    GLuint m_textureU;
    GLuint m_textureV;

    // 顶点缓冲（使用 Qt 封装类）
    QOpenGLBuffer m_vbo;
    QOpenGLVertexArrayObject m_vao;

    // 帧数据缓冲（线程安全）
    QMutex m_frameMutex;
    QByteArray m_bufferY;
    QByteArray m_bufferU;
    QByteArray m_bufferV;
    int m_frameWidth;
    int m_frameHeight;
    int m_linesize[4];
    bool m_frameUpdated;
    bool m_texturesInitialized;

    // 视频尺寸
    QSize m_videoSize;

    // Shader uniform locations
    GLint m_locTextureY;
    GLint m_locTextureU;
    GLint m_locTextureV;

    // ========== 缩放模式和视图控制 ==========
    ScaleMode m_scaleMode;      // 当前缩放模式
    float m_zoomFactor;         // 缩放因子 (1.0 ~ 10.0)
    QPointF m_panOffset;        // 平移偏移（像素）
};

#endif // VIDEOGLWIDGET_H
