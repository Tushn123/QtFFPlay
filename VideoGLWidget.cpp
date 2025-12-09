/*
 * VideoGLWidget.cpp
 * 
 * Qt OpenGL 视频渲染组件实现
 * 使用 GPU Shader 进行 YUV420P 到 RGB 的转换
 * 兼容 Qt 5 和 OpenGL ES 2.0+
 */

#include "VideoGLWidget.h"
#include <QDebug>

// 避免 C 头文件中的 class 关键字冲突
#define class class_name
extern "C" {
#include "player/ff_ffplayer.h"
}
#undef class

// 顶点着色器 - 兼容 OpenGL ES 2.0 / OpenGL 2.1+
static const char *vertexShaderSource = R"(
    attribute vec2 aPos;
    attribute vec2 aTexCoord;
    
    varying vec2 TexCoord;
    
    void main()
    {
        gl_Position = vec4(aPos, 0.0, 1.0);
        TexCoord = aTexCoord;
    }
)";

// 片段着色器 - YUV420P 到 RGB 转换，兼容 OpenGL ES 2.0 / OpenGL 2.1+
static const char *fragmentShaderSource = R"(
    #ifdef GL_ES
    precision mediump float;
    #endif
    
    varying vec2 TexCoord;
    
    uniform sampler2D textureY;
    uniform sampler2D textureU;
    uniform sampler2D textureV;
    
    void main()
    {
        // 采样 YUV 值
        float y = texture2D(textureY, TexCoord).r;
        float u = texture2D(textureU, TexCoord).r - 0.5;
        float v = texture2D(textureV, TexCoord).r - 0.5;
        
        // YUV 到 RGB 转换 (BT.601)
        float r = y + 1.402 * v;
        float g = y - 0.344 * u - 0.714 * v;
        float b = y + 1.772 * u;
        
        gl_FragColor = vec4(r, g, b, 1.0);
    }
)";

// 顶点数据：位置和纹理坐标
static const float vertices[] = {
    // 位置          // 纹理坐标
    -1.0f,  1.0f,   0.0f, 0.0f,  // 左上
     1.0f,  1.0f,   1.0f, 0.0f,  // 右上
    -1.0f, -1.0f,   0.0f, 1.0f,  // 左下
     1.0f, -1.0f,   1.0f, 1.0f,  // 右下
};

VideoGLWidget::VideoGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_program(nullptr)
    , m_textureY(0)
    , m_textureU(0)
    , m_textureV(0)
    , m_vbo(QOpenGLBuffer::VertexBuffer)
    , m_frameWidth(0)
    , m_frameHeight(0)
    , m_frameUpdated(false)
    , m_texturesInitialized(false)
    , m_locTextureY(-1)
    , m_locTextureU(-1)
    , m_locTextureV(-1)
{
    memset(m_linesize, 0, sizeof(m_linesize));
    
    // 设置背景色
    setAutoFillBackground(false);
    
    // 连接跨线程信号
    connect(this, &VideoGLWidget::frameReady, this, &VideoGLWidget::onFrameReady, Qt::QueuedConnection);
}

VideoGLWidget::~VideoGLWidget()
{
    makeCurrent();
    cleanupGL();
    doneCurrent();
}

void VideoGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    
    qDebug() << "[VideoGLWidget] OpenGL version:" << (const char*)glGetString(GL_VERSION);
    qDebug() << "[VideoGLWidget] GLSL version:" << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    
    // 设置清除色为黑色
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    initShaders();
    initGeometry();
    initTextures();
}

void VideoGLWidget::initShaders()
{
    m_program = new QOpenGLShaderProgram(this);
    
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        qWarning() << "[VideoGLWidget] Vertex shader compilation failed:" << m_program->log();
        return;
    }
    
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
        qWarning() << "[VideoGLWidget] Fragment shader compilation failed:" << m_program->log();
        return;
    }
    
    // 绑定属性位置（在链接之前）
    m_program->bindAttributeLocation("aPos", 0);
    m_program->bindAttributeLocation("aTexCoord", 1);
    
    if (!m_program->link()) {
        qWarning() << "[VideoGLWidget] Shader program linking failed:" << m_program->log();
        return;
    }
    
    // 获取 uniform 位置
    m_locTextureY = m_program->uniformLocation("textureY");
    m_locTextureU = m_program->uniformLocation("textureU");
    m_locTextureV = m_program->uniformLocation("textureV");
    
    qDebug() << "[VideoGLWidget] Shaders initialized successfully";
}

void VideoGLWidget::initGeometry()
{
    // 创建并绑定 VAO（如果支持）
    if (m_vao.create()) {
        m_vao.bind();
    }
    
    // 创建并设置 VBO
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices, sizeof(vertices));
    
    // 设置顶点属性
    m_program->bind();
    
    // 位置属性
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    
    // 纹理坐标属性
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    
    m_program->release();
    m_vbo.release();
    
    if (m_vao.isCreated()) {
        m_vao.release();
    }
    
    qDebug() << "[VideoGLWidget] Geometry initialized, VAO supported:" << m_vao.isCreated();
}

void VideoGLWidget::initTextures()
{
    // 创建 YUV 纹理
    glGenTextures(1, &m_textureY);
    glGenTextures(1, &m_textureU);
    glGenTextures(1, &m_textureV);
    
    // 配置纹理参数
    auto setupTexture = [this](GLuint texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    
    setupTexture(m_textureY);
    setupTexture(m_textureU);
    setupTexture(m_textureV);
    
    qDebug() << "[VideoGLWidget] Textures initialized";
}

void VideoGLWidget::cleanupGL()
{
    if (m_program) {
        delete m_program;
        m_program = nullptr;
    }
    
    if (m_textureY) {
        glDeleteTextures(1, &m_textureY);
        m_textureY = 0;
    }
    if (m_textureU) {
        glDeleteTextures(1, &m_textureU);
        m_textureU = 0;
    }
    if (m_textureV) {
        glDeleteTextures(1, &m_textureV);
        m_textureV = 0;
    }
    
    m_vbo.destroy();
    m_vao.destroy();
}

void VideoGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VideoGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    
    // 检查是否有帧数据
    QMutexLocker locker(&m_frameMutex);
    
    if (m_bufferY.isEmpty() || m_frameWidth == 0 || m_frameHeight == 0) {
        return;
    }
    
    // 如果帧数据更新了，上传到纹理
    if (m_frameUpdated) {
        updateTextures();
        m_frameUpdated = false;
    }
    
    locker.unlock();
    
    if (!m_program || !m_texturesInitialized) {
        return;
    }
    
    // 计算保持宽高比的视口
    int widgetW = width();
    int widgetH = height();
    float videoAspect = (float)m_videoSize.width() / m_videoSize.height();
    float widgetAspect = (float)widgetW / widgetH;
    
    int viewportX, viewportY, viewportW, viewportH;
    if (videoAspect > widgetAspect) {
        // 视频更宽，上下留黑边
        viewportW = widgetW;
        viewportH = (int)(widgetW / videoAspect);
        viewportX = 0;
        viewportY = (widgetH - viewportH) / 2;
    } else {
        // 视频更高，左右留黑边
        viewportH = widgetH;
        viewportW = (int)(widgetH * videoAspect);
        viewportX = (widgetW - viewportW) / 2;
        viewportY = 0;
    }
    
    glViewport(viewportX, viewportY, viewportW, viewportH);
    
    // 绑定着色器程序
    m_program->bind();
    
    // 绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureY);
    m_program->setUniformValue(m_locTextureY, 0);
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textureU);
    m_program->setUniformValue(m_locTextureU, 1);
    
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_textureV);
    m_program->setUniformValue(m_locTextureV, 2);
    
    // 绑定 VAO 或手动设置属性
    if (m_vao.isCreated()) {
        m_vao.bind();
    } else {
        // 手动绑定 VBO 和设置属性
        m_vbo.bind();
        m_program->enableAttributeArray(0);
        m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
        m_program->enableAttributeArray(1);
        m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    }
    
    // 绘制
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // 解绑
    if (m_vao.isCreated()) {
        m_vao.release();
    } else {
        m_vbo.release();
    }
    
    m_program->release();
    
    // 恢复完整视口
    glViewport(0, 0, widgetW, widgetH);
}

void VideoGLWidget::updateTextures()
{
    // 此函数在 paintGL 中调用，已经持有锁
    
    // Y 平面 - 使用 GL_LUMINANCE 以兼容 OpenGL ES
    glBindTexture(GL_TEXTURE_2D, m_textureY);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_frameWidth, m_frameHeight, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, m_bufferY.constData());
    
    // U 平面 (1/4 大小)
    glBindTexture(GL_TEXTURE_2D, m_textureU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_frameWidth / 2, m_frameHeight / 2, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, m_bufferU.constData());
    
    // V 平面 (1/4 大小)
    glBindTexture(GL_TEXTURE_2D, m_textureV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_frameWidth / 2, m_frameHeight / 2, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, m_bufferV.constData());
    
    m_texturesInitialized = true;
}

void VideoGLWidget::updateFrame(const FFPVideoFrame *frame)
{
    if (!frame || !frame->data[0] || frame->width <= 0 || frame->height <= 0) {
        return;
    }
    
    // 目前只支持 YUV420P 格式 (format = 0 in AVPixelFormat)
    // AV_PIX_FMT_YUV420P = 0
    if (frame->format != 0) {
        static bool warned = false;
        if (!warned) {
            qWarning() << "[VideoGLWidget] Unsupported pixel format:" << frame->format << "(only YUV420P supported)";
            warned = true;
        }
        return;
    }
    
    QMutexLocker locker(&m_frameMutex);
    
    // 检查尺寸变化
    bool sizeChanged = (m_frameWidth != frame->width || m_frameHeight != frame->height);
    
    m_frameWidth = frame->width;
    m_frameHeight = frame->height;
    m_linesize[0] = frame->linesize[0];
    m_linesize[1] = frame->linesize[1];
    m_linesize[2] = frame->linesize[2];
    
    // 复制 Y 平面数据（按实际像素宽度，去掉padding）
    int ySize = frame->width * frame->height;
    if (m_bufferY.size() != ySize) {
        m_bufferY.resize(ySize);
    }
    // 逐行复制以去除 linesize padding
    uint8_t *dst = (uint8_t*)m_bufferY.data();
    const uint8_t *src = frame->data[0];
    for (int i = 0; i < frame->height; i++) {
        memcpy(dst, src, frame->width);
        dst += frame->width;
        src += frame->linesize[0];
    }
    
    // 复制 U 平面数据
    int uvWidth = frame->width / 2;
    int uvHeight = frame->height / 2;
    int uSize = uvWidth * uvHeight;
    if (m_bufferU.size() != uSize) {
        m_bufferU.resize(uSize);
    }
    dst = (uint8_t*)m_bufferU.data();
    src = frame->data[1];
    for (int i = 0; i < uvHeight; i++) {
        memcpy(dst, src, uvWidth);
        dst += uvWidth;
        src += frame->linesize[1];
    }
    
    // 复制 V 平面数据
    int vSize = uvWidth * uvHeight;
    if (m_bufferV.size() != vSize) {
        m_bufferV.resize(vSize);
    }
    dst = (uint8_t*)m_bufferV.data();
    src = frame->data[2];
    for (int i = 0; i < uvHeight; i++) {
        memcpy(dst, src, uvWidth);
        dst += uvWidth;
        src += frame->linesize[2];
    }
    
    m_frameUpdated = true;
    
    // 更新视频尺寸
    if (sizeChanged) {
        m_videoSize = QSize(frame->width, frame->height);
        locker.unlock();
        emit videoSizeChanged(frame->width, frame->height);
    } else {
        locker.unlock();
    }
    
    // 发送信号请求重绘（跨线程安全）
    emit frameReady();
}

void VideoGLWidget::clearFrame()
{
    QMutexLocker locker(&m_frameMutex);
    m_bufferY.clear();
    m_bufferU.clear();
    m_bufferV.clear();
    m_frameWidth = 0;
    m_frameHeight = 0;
    m_frameUpdated = false;
    m_texturesInitialized = false;
    locker.unlock();
    
    update();
}

void VideoGLWidget::onFrameReady()
{
    update();
}
