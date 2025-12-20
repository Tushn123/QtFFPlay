/*
 * VideoGLWidget.cpp
 * 
 * Qt OpenGL 视频渲染组件实现
 * 使用 GPU Shader 进行 YUV420P 到 RGB 的转换
 * 支持多种缩放模式、画面缩放和平移
 * 兼容 Qt 5 和 OpenGL ES 2.0+
 */

#include "VideoGLWidget.h"
#include <QDebug>
#include <QtMath>

// 避免 C 头文件中的 class 关键字冲突
#define class class_name
extern "C" {
#include "player/ff_ffplayer.h"
}
#undef class

// 缩放范围常量
static const float ZOOM_MIN = 0.5f;
static const float ZOOM_MAX = 10.0f;
static const float ZOOM_STEP = 0.1f;  // 每次滚轮缩放步进

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
    , m_scaleMode(ScaleMode::Stretch)
    , m_zoomFactor(1.0f)
    , m_panOffset(0, 0)
    , m_pendingFrames(0)
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

// ========== 缩放模式和视图控制 ==========

void VideoGLWidget::setScaleMode(ScaleMode mode)
{
    if (m_scaleMode != mode) {
        m_scaleMode = mode;
        // 切换模式时重置平移
        m_panOffset = QPointF(0, 0);
        emit scaleModeChanged(mode);
        update();
    }
}

void VideoGLWidget::setZoom(float factor)
{
    factor = qBound(ZOOM_MIN, factor, ZOOM_MAX);
    if (!qFuzzyCompare(m_zoomFactor, factor)) {
        m_zoomFactor = factor;
        
        // 如果缩小到 1.0 或更小，重置平移
        if (m_zoomFactor <= 1.0f) {
            m_panOffset = QPointF(0, 0);
        } else {
            clampPanOffset();
        }
        
        emit zoomChanged(m_zoomFactor);
        update();
    }
}

void VideoGLWidget::resetView()
{
    m_zoomFactor = 1.0f;
    m_panOffset = QPointF(0, 0);
    emit zoomChanged(m_zoomFactor);
    update();
}

void VideoGLWidget::setPan(const QPointF &offset)
{
    m_panOffset = offset;
    clampPanOffset();
    update();
}

QPointF VideoGLWidget::maxPan() const
{
    if (m_videoSize.isEmpty() || m_zoomFactor <= 1.0f) {
        return QPointF(0, 0);
    }
    
    int widgetW = width();
    int widgetH = height();
    
    float videoAspect = (float)m_videoSize.width() / m_videoSize.height();
    float widgetAspect = (float)widgetW / widgetH;
    
    float baseW, baseH;
    
    switch (m_scaleMode) {
    case ScaleMode::Stretch:
        baseW = widgetW;
        baseH = widgetH;
        break;
    case ScaleMode::Fill:
        if (videoAspect > widgetAspect) {
            baseH = widgetH;
            baseW = baseH * videoAspect;
        } else {
            baseW = widgetW;
            baseH = baseW / videoAspect;
        }
        break;
    case ScaleMode::Fit:
    default:
        if (videoAspect > widgetAspect) {
            baseW = widgetW;
            baseH = baseW / videoAspect;
        } else {
            baseH = widgetH;
            baseW = baseH * videoAspect;
        }
        break;
    }
    
    qreal zoomedW = baseW * m_zoomFactor;
    qreal zoomedH = baseH * m_zoomFactor;
    
    qreal maxPanX = qMax(0.0, (zoomedW - widgetW) / 2.0);
    qreal maxPanY = qMax(0.0, (zoomedH - widgetH) / 2.0);
    
    return QPointF(maxPanX, maxPanY);
}

void VideoGLWidget::zoomAt(float factor, const QPointF &center)
{
    factor = qBound(ZOOM_MIN, factor, ZOOM_MAX);
    if (qFuzzyCompare(m_zoomFactor, factor)) {
        return;
    }
    
    // 计算当前视口
    QRectF oldViewport = calculateViewport(width(), height());
    
    // 计算鼠标在当前视口中的相对位置 (0~1)
    float relX = (center.x() - oldViewport.x()) / oldViewport.width();
    float relY = (center.y() - oldViewport.y()) / oldViewport.height();
    
    // 限制相对位置在合理范围内
    relX = qBound(0.0f, relX, 1.0f);
    relY = qBound(0.0f, relY, 1.0f);
    
    // 计算缩放比例
    float oldZoom = m_zoomFactor;
    m_zoomFactor = factor;
    float zoomRatio = m_zoomFactor / oldZoom;
    
    // 计算缩放前后视口尺寸变化
    float oldW = oldViewport.width();
    float oldH = oldViewport.height();
    float newW = oldW * zoomRatio;
    float newH = oldH * zoomRatio;
    
    // 调整平移偏移，使缩放中心点保持不变
    m_panOffset.setX(m_panOffset.x() - (newW - oldW) * relX + (newW - oldW) / 2.0f);
    m_panOffset.setY(m_panOffset.y() - (newH - oldH) * relY + (newH - oldH) / 2.0f);
    
    // 如果缩小到 1.0 或更小，重置平移
    if (m_zoomFactor <= 1.0f) {
        m_panOffset = QPointF(0, 0);
    } else {
        clampPanOffset();
    }
    
    emit zoomChanged(m_zoomFactor);
    update();
}

void VideoGLWidget::clampPanOffset()
{
    if (m_videoSize.isEmpty() || m_zoomFactor <= 1.0f) {
        m_panOffset = QPointF(0, 0);
        return;
    }
    
    // 计算基础视口大小
    int widgetW = width();
    int widgetH = height();
    
    float videoAspect = (float)m_videoSize.width() / m_videoSize.height();
    float widgetAspect = (float)widgetW / widgetH;
    
    float baseW, baseH;
    
    switch (m_scaleMode) {
    case ScaleMode::Stretch:
        baseW = widgetW;
        baseH = widgetH;
        break;
    case ScaleMode::Fill:
        if (videoAspect > widgetAspect) {
            baseH = widgetH;
            baseW = baseH * videoAspect;
        } else {
            baseW = widgetW;
            baseH = baseW / videoAspect;
        }
        break;
    case ScaleMode::Fit:
    default:
        if (videoAspect > widgetAspect) {
            baseW = widgetW;
            baseH = baseW / videoAspect;
        } else {
            baseH = widgetH;
            baseW = baseH * videoAspect;
        }
        break;
    }
    
    // 缩放后的尺寸
    qreal zoomedW = baseW * m_zoomFactor;
    qreal zoomedH = baseH * m_zoomFactor;
    
    // 最大平移范围：允许画面移动到边缘与窗口中心对齐
    qreal maxPanX = qMax(0.0, (zoomedW - widgetW) / 2.0);
    qreal maxPanY = qMax(0.0, (zoomedH - widgetH) / 2.0);
    
    m_panOffset.setX(qBound(-maxPanX, m_panOffset.x(), maxPanX));
    m_panOffset.setY(qBound(-maxPanY, m_panOffset.y(), maxPanY));
}

QRectF VideoGLWidget::calculateViewport(int widgetW, int widgetH)
{
    if (m_videoSize.isEmpty()) {
        return QRectF(0, 0, widgetW, widgetH);
    }
    
    float videoAspect = (float)m_videoSize.width() / m_videoSize.height();
    float widgetAspect = (float)widgetW / widgetH;
    
    float baseW, baseH, baseX, baseY;
    
    switch (m_scaleMode) {
    case ScaleMode::Stretch:
        // 拉伸填充整个窗口
        baseW = widgetW;
        baseH = widgetH;
        baseX = 0;
        baseY = 0;
        break;
        
    case ScaleMode::Fill:
        // 保持比例，裁剪填充（无黑边）
        if (videoAspect > widgetAspect) {
            // 视频更宽，左右裁剪
            baseH = widgetH;
            baseW = baseH * videoAspect;
        } else {
            // 视频更高，上下裁剪
            baseW = widgetW;
            baseH = baseW / videoAspect;
        }
        baseX = (widgetW - baseW) / 2.0f;
        baseY = (widgetH - baseH) / 2.0f;
        break;
        
    case ScaleMode::Fit:
    default:
        // 保持比例，适应窗口（可能有黑边）
        if (videoAspect > widgetAspect) {
            // 视频更宽，上下留黑边
            baseW = widgetW;
            baseH = baseW / videoAspect;
        } else {
            // 视频更高，左右留黑边
            baseH = widgetH;
            baseW = baseH * videoAspect;
        }
        baseX = (widgetW - baseW) / 2.0f;
        baseY = (widgetH - baseH) / 2.0f;
        break;
    }
    
    // 应用缩放
    float zoomedW = baseW * m_zoomFactor;
    float zoomedH = baseH * m_zoomFactor;
    
    // 缩放中心调整
    float centerX = widgetW / 2.0f;
    float centerY = widgetH / 2.0f;
    
    // 计算缩放后的位置（以窗口中心为缩放中心）
    float zoomedX = centerX - zoomedW / 2.0f;
    float zoomedY = centerY - zoomedH / 2.0f;
    
    // 应用平移
    zoomedX += m_panOffset.x();
    zoomedY += m_panOffset.y();
    
    return QRectF(zoomedX, zoomedY, zoomedW, zoomedH);
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
    
    int widgetW = width();
    int widgetH = height();
    
    // 计算视口
    QRectF viewport = calculateViewport(widgetW, widgetH);
    
    // 设置裁剪区域（防止绘制到窗口外）
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, widgetW, widgetH);
    
    // OpenGL 视口 Y 坐标从底部开始，需要翻转
    int glY = widgetH - (int)viewport.y() - (int)viewport.height();
    glViewport((int)viewport.x(), glY, (int)viewport.width(), (int)viewport.height());
    
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
    
    glDisable(GL_SCISSOR_TEST);
    
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

// ========== 帧数据更新 ==========

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
    
    // 增加待渲染帧计数
    m_pendingFrames.fetchAndAddRelaxed(1);
    
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
    
    // 重置视图状态
    m_videoSize = QSize();
    resetView();
    
    update();
}

QImage VideoGLWidget::getCurrentFrame()
{
    // 先快速复制数据，尽快释放锁，避免阻塞帧更新
    QByteArray yBuffer, uBuffer, vBuffer;
    int width, height;
    int linesize[3];
    
    {
        QMutexLocker locker(&m_frameMutex);
        
        if (m_frameWidth <= 0 || m_frameHeight <= 0 || 
            m_bufferY.isEmpty() || m_bufferU.isEmpty() || m_bufferV.isEmpty()) {
            return QImage();
        }
        
        // 快速复制数据
        width = m_frameWidth;
        height = m_frameHeight;
        yBuffer = m_bufferY;
        uBuffer = m_bufferU;
        vBuffer = m_bufferV;
        linesize[0] = m_linesize[0] > 0 ? m_linesize[0] : width;
        linesize[1] = m_linesize[1] > 0 ? m_linesize[1] : width / 2;
        linesize[2] = m_linesize[2] > 0 ? m_linesize[2] : width / 2;
    }
    // 锁已释放，可以安全地进行耗时的 YUV->RGB 转换
    
    // 缩小预览图尺寸以加快转换速度（预览不需要全分辨率）
    int previewWidth = qMin(width, 320);
    int previewHeight = height * previewWidth / width;
    int stepX = width / previewWidth;
    int stepY = height / previewHeight;
    
    QImage image(previewWidth, previewHeight, QImage::Format_RGB888);
    
    const uchar *yData = reinterpret_cast<const uchar*>(yBuffer.constData());
    const uchar *uData = reinterpret_cast<const uchar*>(uBuffer.constData());
    const uchar *vData = reinterpret_cast<const uchar*>(vBuffer.constData());
    
    for (int py = 0; py < previewHeight; ++py) {
        int srcY = py * stepY;
        uchar *rgb = image.scanLine(py);
        
        for (int px = 0; px < previewWidth; ++px) {
            int srcX = px * stepX;
            
            int Y = yData[srcY * linesize[0] + srcX];
            int U = uData[(srcY / 2) * linesize[1] + (srcX / 2)];
            int V = vData[(srcY / 2) * linesize[2] + (srcX / 2)];
            
            // YUV to RGB (BT.601)
            int C = Y - 16;
            int D = U - 128;
            int E = V - 128;
            
            int R = qBound(0, (298 * C + 409 * E + 128) >> 8, 255);
            int G = qBound(0, (298 * C - 100 * D - 208 * E + 128) >> 8, 255);
            int B = qBound(0, (298 * C + 516 * D + 128) >> 8, 255);
            
            rgb[px * 3 + 0] = R;
            rgb[px * 3 + 1] = G;
            rgb[px * 3 + 2] = B;
        }
    }
    
    return image;
}

void VideoGLWidget::onFrameReady()
{
    // 减少待渲染帧计数
    m_pendingFrames.fetchAndSubRelaxed(1);
    update();
}

bool VideoGLWidget::hasPendingFrames() const
{
    return m_pendingFrames.loadRelaxed() > 0;
}

void VideoGLWidget::forceRepaint()
{
    // 在模态循环中强制重绘
    if (m_pendingFrames.loadRelaxed() > 0) {
        m_pendingFrames.storeRelaxed(0);
        repaint();
    }
}
