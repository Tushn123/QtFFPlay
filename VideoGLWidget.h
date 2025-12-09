/*
 * VideoGLWidget.h
 * 
 * Qt OpenGL 视频渲染组件
 * 支持 YUV420P 格式的高效 GPU 渲染
 */

#ifndef VIDEOGLWIDGET_H
#define VIDEOGLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMutex>

// Forward declaration
struct FFPVideoFrame;

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

signals:
    /**
     * 视频尺寸变化信号
     */
    void videoSizeChanged(int width, int height);

    /**
     * 内部信号：请求更新帧（跨线程）
     */
    void frameReady();

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
};

#endif // VIDEOGLWIDGET_H

