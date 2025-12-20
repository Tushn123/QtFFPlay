#include "ThumbnailExtractor.h"
#include <QDebug>
#include <QMutexLocker>

// MSVC 兼容的错误字符串转换（av_err2str 在 MSVC C++ 中不可用）
static inline QString ffmpegError(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// ============ 硬件加速辅助函数 ============

AVHWDeviceType ThumbnailExtractor::getAVHWDeviceType(ThumbnailHWAccelType type)
{
    switch (type) {
    case ThumbnailHWAccelType::DXVA2:
        return AV_HWDEVICE_TYPE_DXVA2;
    case ThumbnailHWAccelType::D3D11VA:
        return AV_HWDEVICE_TYPE_D3D11VA;
    case ThumbnailHWAccelType::CUDA:
        return AV_HWDEVICE_TYPE_CUDA;
    case ThumbnailHWAccelType::VAAPI:
        return AV_HWDEVICE_TYPE_VAAPI;
    case ThumbnailHWAccelType::VDPAU:
        return AV_HWDEVICE_TYPE_VDPAU;
    case ThumbnailHWAccelType::VideoToolbox:
        return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
    case ThumbnailHWAccelType::QSV:
        return AV_HWDEVICE_TYPE_QSV;
    default:
        return AV_HWDEVICE_TYPE_NONE;
    }
}

static const char* getHWAccelName(ThumbnailHWAccelType type)
{
    switch (type) {
    case ThumbnailHWAccelType::None: return "none";
    case ThumbnailHWAccelType::Auto: return "auto";
    case ThumbnailHWAccelType::DXVA2: return "dxva2";
    case ThumbnailHWAccelType::D3D11VA: return "d3d11va";
    case ThumbnailHWAccelType::CUDA: return "cuda";
    case ThumbnailHWAccelType::VAAPI: return "vaapi";
    case ThumbnailHWAccelType::VDPAU: return "vdpau";
    case ThumbnailHWAccelType::VideoToolbox: return "videotoolbox";
    case ThumbnailHWAccelType::QSV: return "qsv";
    default: return "unknown";
    }
}

// 自动选择最佳硬件加速
static ThumbnailHWAccelType selectBestHWAccel()
{
    static const ThumbnailHWAccelType preferred[] = {
#ifdef _WIN32
        ThumbnailHWAccelType::D3D11VA,
        ThumbnailHWAccelType::DXVA2,
#elif defined(__APPLE__)
        ThumbnailHWAccelType::VideoToolbox,
#elif defined(__linux__)
        ThumbnailHWAccelType::VAAPI,
        ThumbnailHWAccelType::VDPAU,
#endif
        ThumbnailHWAccelType::CUDA,
        ThumbnailHWAccelType::QSV,
    };
    
    for (auto type : preferred) {
        AVHWDeviceType avType = ThumbnailExtractor::getAVHWDeviceType(type);
        if (avType != AV_HWDEVICE_TYPE_NONE) {
            AVBufferRef *testCtx = nullptr;
            int ret = av_hwdevice_ctx_create(&testCtx, avType, nullptr, nullptr, 0);
            if (ret >= 0) {
                av_buffer_unref(&testCtx);
                qDebug() << "[ThumbnailExtractor] Auto-selected hwaccel:" << getHWAccelName(type);
                return type;
            }
        }
    }
    return ThumbnailHWAccelType::None;
}

// ============ ThumbnailExtractor 实现 ============

ThumbnailExtractor::ThumbnailExtractor(QObject *parent)
    : QObject(parent)
    , m_cache(50)  // 缓存最多50个缩略图
{
    // 创建工作线程
    m_workerThread = new QThread(this);
    this->moveToThread(m_workerThread);
    m_workerThread->start();
}

ThumbnailExtractor::~ThumbnailExtractor()
{
    close();
    
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

bool ThumbnailExtractor::open(const QString &filePath)
{
    QMutexLocker locker(&m_mutex);
    
    // 如果已经打开了相同的文件，直接返回
    if (m_formatCtx && m_filePath == filePath) {
        return true;
    }
    
    // 关闭之前的
    if (m_formatCtx) {
        locker.unlock();
        close();
        locker.relock();
    }
    
    m_filePath = filePath;
    m_cache.clear();
    
    // 打开文件
    int ret = avformat_open_input(&m_formatCtx, filePath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        qWarning() << "[ThumbnailExtractor] Failed to open file:" << filePath;
        return false;
    }
    
    // 获取流信息
    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        qWarning() << "[ThumbnailExtractor] Failed to find stream info";
        avformat_close_input(&m_formatCtx);
        return false;
    }
    
    // 查找视频流
    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            break;
        }
    }
    
    if (m_videoStreamIndex < 0) {
        qWarning() << "[ThumbnailExtractor] No video stream found";
        avformat_close_input(&m_formatCtx);
        return false;
    }
    
    // 获取解码器
    AVStream *videoStream = m_formatCtx->streams[m_videoStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        qWarning() << "[ThumbnailExtractor] Codec not found";
        avformat_close_input(&m_formatCtx);
        return false;
    }
    
    // 创建解码器上下文
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qWarning() << "[ThumbnailExtractor] Failed to allocate codec context";
        avformat_close_input(&m_formatCtx);
        return false;
    }
    
    // 复制参数
    ret = avcodec_parameters_to_context(m_codecCtx, videoStream->codecpar);
    if (ret < 0) {
        qWarning() << "[ThumbnailExtractor] Failed to copy codec params";
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_formatCtx);
        return false;
    }
    
    // 配置硬件加速（如果启用）
    m_hwAccelFailed = false;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    if (m_hwAccelType != ThumbnailHWAccelType::None) {
        bool hwOk = configureHWAccel(codec);
        qDebug() << "[ThumbnailExtractor] HWAccel config:" << (hwOk ? "SUCCESS" : "FALLBACK_TO_SW")
                 << ", type:" << getHWAccelName(m_hwAccelType);
    }
    
    // 打开解码器
    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        qWarning() << "[ThumbnailExtractor] Failed to open codec";
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_formatCtx);
        return false;
    }
    
    // 保存视频信息
    m_videoWidth = m_codecCtx->width;
    m_videoHeight = m_codecCtx->height;
    
    // 计算时长（毫秒）
    if (m_formatCtx->duration != AV_NOPTS_VALUE) {
        m_duration = m_formatCtx->duration / (AV_TIME_BASE / 1000);
    } else if (videoStream->duration != AV_NOPTS_VALUE) {
        AVRational msTimeBase = {1, 1000};
        m_duration = av_rescale_q(videoStream->duration, videoStream->time_base, msTimeBase);
    }
    
    qDebug() << "[ThumbnailExtractor] Opened:" << filePath 
             << ", size:" << m_videoWidth << "x" << m_videoHeight
             << ", duration:" << m_duration << "ms"
             << ", hwaccel:" << getHWAccelName(m_hwAccelType);
    
    return true;
}

void ThumbnailExtractor::close()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
    
    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
    }
    
    // 释放硬件设备上下文
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
    
    m_videoStreamIndex = -1;
    m_duration = 0;
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_filePath.clear();
    m_cache.clear();
    m_pendingPositions.clear();
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_hwAccelFailed = false;
}

// ============ 硬件加速配置（参考 FFPlayer::configure_hwaccel）============

bool ThumbnailExtractor::configureHWAccel(const AVCodec *codec)
{
    ThumbnailHWAccelType hwType = m_hwAccelType;
    
    // 自动选择
    if (hwType == ThumbnailHWAccelType::Auto) {
        hwType = selectBestHWAccel();
        if (hwType == ThumbnailHWAccelType::None) {
            qDebug() << "[ThumbnailExtractor] No suitable hwaccel found, using software";
            return false;
        }
    }
    
    AVHWDeviceType avHwType = getAVHWDeviceType(hwType);
    if (avHwType == AV_HWDEVICE_TYPE_NONE) {
        return false;
    }
    
    // 检查编解码器是否支持该硬件加速
    const AVCodecHWConfig *config = nullptr;
    for (int i = 0; (config = avcodec_get_hw_config(codec, i)) != nullptr; i++) {
        if (config->device_type == avHwType &&
            (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
            break;
        }
    }
    
    if (!config) {
        qDebug() << "[ThumbnailExtractor] Codec" << codec->name 
                 << "does not support" << av_hwdevice_get_type_name(avHwType);
        return false;
    }
    
    // 创建硬件设备上下文
    int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, avHwType, nullptr, nullptr, 0);
    if (ret < 0) {
        qWarning() << "[ThumbnailExtractor] Failed to create hwdevice:" 
                   << av_hwdevice_get_type_name(avHwType);
        return false;
    }
    
    // 设置解码器的硬件设备上下文
    m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
    if (!m_codecCtx->hw_device_ctx) {
        av_buffer_unref(&m_hwDeviceCtx);
        return false;
    }
    
    // 保存硬件像素格式
    m_hwPixFmt = config->pix_fmt;
    
    qDebug() << "[ThumbnailExtractor] HWAccel configured:" 
             << getHWAccelName(hwType) << ", pix_fmt:" << av_get_pix_fmt_name(m_hwPixFmt);
    
    return true;
}

int ThumbnailExtractor::hwFrameToSw(AVFrame *hwFrame, AVFrame *swFrame)
{
    // 将 GPU 帧数据传输到 CPU
    int ret = av_hwframe_transfer_data(swFrame, hwFrame, 0);
    if (ret < 0) {
        qWarning() << "[ThumbnailExtractor] HW frame transfer failed:" << ffmpegError(ret);
        return ret;
    }
    
    // 复制帧属性
    ret = av_frame_copy_props(swFrame, hwFrame);
    if (ret < 0) {
        qWarning() << "[ThumbnailExtractor] Frame props copy failed:" << ffmpegError(ret);
        return ret;
    }
    
    return 0;
}

// ============ 缩略图请求和提取 ============

void ThumbnailExtractor::requestThumbnail(qint64 positionMs)
{
    // 量化位置到1秒间隔，减少解码次数
    qint64 quantizedPos = (positionMs / 1000) * 1000;
    
    {
        QMutexLocker locker(&m_mutex);
        
        // 1. 检查缓存 - 命中则直接返回
        if (QImage *cached = m_cache.object(quantizedPos)) {
            emit thumbnailReady(*cached, positionMs);
            return;
        }
        
        // 2. 检查是否已经在提取中 - 避免重复请求
        if (m_pendingPositions.contains(quantizedPos)) {
            return;  // 已经在队列中，跳过
        }
        
        // 3. 标记为正在提取
        m_pendingPositions.insert(quantizedPos);
    }
    
    // 4. 异步提取
    QMetaObject::invokeMethod(this, "doExtract", Qt::QueuedConnection,
                              Q_ARG(qint64, quantizedPos));
}

void ThumbnailExtractor::doExtract(qint64 positionMs)
{
    QImage image = extractFrame(positionMs);
    
    {
        QMutexLocker locker(&m_mutex);
        
        // 从待处理集合中移除
        m_pendingPositions.remove(positionMs);
        
        // 加入缓存（即使是空图像也缓存，避免重复解码失败的位置）
        if (!image.isNull()) {
            m_cache.insert(positionMs, new QImage(image));
        }
    }
    
    emit thumbnailReady(image, positionMs);
}

QImage ThumbnailExtractor::extractFrame(qint64 positionMs)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_formatCtx || !m_codecCtx || m_videoStreamIndex < 0) {
        return QImage();
    }
    
    AVStream *videoStream = m_formatCtx->streams[m_videoStreamIndex];
    
    // 边界检查
    if (positionMs < 0) positionMs = 0;
    if (m_duration > 0 && positionMs > m_duration) positionMs = m_duration - 100;
    
    // 转换毫秒到时间戳
    AVRational msTimeBase = {1, 1000};
    int64_t timestamp = av_rescale_q(positionMs, msTimeBase, videoStream->time_base);
    
    // Seek 到目标位置（向后找最近的关键帧）
    int ret = av_seek_frame(m_formatCtx, m_videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // 如果向后 seek 失败，尝试 seek 到文件开头
        ret = av_seek_frame(m_formatCtx, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            qWarning() << "[ThumbnailExtractor] Seek failed to" << positionMs << "ms";
            return QImage();
        }
    }
    
    // 清空解码器缓冲
    avcodec_flush_buffers(m_codecCtx);
    
    // 分配帧和包
    AVFrame *frame = av_frame_alloc();
    AVFrame *swFrame = nullptr;  // 用于硬件帧转软件帧
    AVPacket *packet = av_packet_alloc();
    QImage result;
    QImage lastValidFrame;  // 保存最后一个有效帧（兜底用）
    
    if (!frame || !packet) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        return QImage();
    }
    
    // 解码直到得到目标位置附近的帧
    int64_t targetPts = timestamp;
    int maxFrames = 150;  // 增加到150帧，处理更大的GOP
    
    while (maxFrames-- > 0) {
        ret = av_read_frame(m_formatCtx, packet);
        if (ret < 0) {
            // 读取失败（可能到达文件末尾），使用最后一个有效帧
            if (!lastValidFrame.isNull()) {
                result = lastValidFrame;
            }
            break;
        }
        
        if (packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }
        
        ret = avcodec_send_packet(m_codecCtx, packet);
        av_packet_unref(packet);
        
        if (ret < 0) {
            continue;
        }
        
        while (ret >= 0) {
            ret = avcodec_receive_frame(m_codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }
            
            // 处理硬件帧：如果是硬件格式，转换到软件帧
            AVFrame *displayFrame = frame;
            if (frame->format == m_hwPixFmt && !m_hwAccelFailed) {
                if (!swFrame) {
                    swFrame = av_frame_alloc();
                    if (!swFrame) {
                        av_frame_unref(frame);
                        continue;
                    }
                }
                
                int hwRet = hwFrameToSw(frame, swFrame);
                if (hwRet < 0) {
                    // 硬件转换失败，标记为失败，后续使用软解码
                    qWarning() << "[ThumbnailExtractor] HW frame transfer failed, falling back to SW";
                    m_hwAccelFailed = true;
                } else {
                    displayFrame = swFrame;
                    av_frame_unref(frame);  // 释放硬件帧引用
                }
            }
            
            // 保存当前帧为最后有效帧（用于兜底）
            lastValidFrame = frameToImage(displayFrame);
            
            // 检查是否到达或超过目标位置
            int64_t framePts = displayFrame->best_effort_timestamp;
            if (framePts >= targetPts) {
                result = lastValidFrame;
                if (displayFrame == swFrame) {
                    av_frame_unref(swFrame);
                } else {
                    av_frame_unref(frame);
                }
                goto done;
            }
            
            if (displayFrame == swFrame) {
                av_frame_unref(swFrame);
            } else {
                av_frame_unref(frame);
            }
        }
    }
    
    // 如果循环结束仍未找到精确匹配，使用最后一个有效帧
    if (result.isNull() && !lastValidFrame.isNull()) {
        result = lastValidFrame;
    }
    
done:
    av_frame_free(&frame);
    av_frame_free(&swFrame);
    av_packet_free(&packet);
    
    return result;
}

QImage ThumbnailExtractor::frameToImage(AVFrame *frame)
{
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        return QImage();
    }
    
    // 计算缩略图尺寸
    int srcW = frame->width;
    int srcH = frame->height;
    int dstW = m_thumbnailWidth;
    int dstH = srcH * dstW / srcW;
    
    // 创建或更新 SwsContext
    m_swsCtx = sws_getCachedContext(
        m_swsCtx,
        srcW, srcH, (AVPixelFormat)frame->format,
        dstW, dstH, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!m_swsCtx) {
        qWarning() << "[ThumbnailExtractor] Failed to create SwsContext";
        return QImage();
    }
    
    // 分配输出缓冲区
    QImage image(dstW, dstH, QImage::Format_RGB888);
    uint8_t *dstData[4] = { image.bits(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };
    
    // 转换
    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, srcH,
              dstData, dstLinesize);
    
    return image;
}

