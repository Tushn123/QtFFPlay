#ifndef THUMBNAILEXTRACTOR_H
#define THUMBNAILEXTRACTOR_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QThread>
#include <QString>
#include <QCache>
#include <QSet>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>  // av_get_pix_fmt_name
}

/**
 * 硬件加速类型（与 FFPlayer 保持一致）
 */
enum class ThumbnailHWAccelType {
    None = 0,           // 软解码
    Auto,               // 自动选择
    DXVA2,              // Windows DXVA2
    D3D11VA,            // Windows D3D11
    CUDA,               // NVIDIA CUDA
    VAAPI,              // Linux VAAPI
    VDPAU,              // Linux VDPAU
    VideoToolbox,       // macOS VideoToolbox
    QSV                 // Intel QSV
};

/**
 * 视频缩略图提取器
 * 独立于播放器，用于提取任意位置的视频帧作为预览
 * 
 * 特性：
 * - 使用独立的 FFmpeg 上下文，不影响播放
 * - 支持硬件加速解码（配置从外部传入，保持封装性）
 * - 异步提取，不阻塞主线程
 * - 内置缓存，避免重复解码
 */
class ThumbnailExtractor : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailExtractor(QObject *parent = nullptr);
    ~ThumbnailExtractor();

    /**
     * 设置硬件加速类型（必须在 open 之前调用）
     * @param type 硬件加速类型
     * 
     * 封装性说明：硬件加速配置由上层（如 PlayerWidget）统一管理，
     * ThumbnailExtractor 只提供接口，不自行决定使用何种加速。
     */
    void setHWAccelType(ThumbnailHWAccelType type) { m_hwAccelType = type; }
    ThumbnailHWAccelType hwAccelType() const { return m_hwAccelType; }

    /**
     * 打开视频文件
     * @param filePath 视频文件路径
     * @return 是否成功
     */
    bool open(const QString &filePath);

    /**
     * 关闭并释放资源
     */
    void close();

    /**
     * 是否已打开
     */
    bool isOpen() const { return m_formatCtx != nullptr; }

    /**
     * 获取视频时长（毫秒）
     */
    qint64 duration() const { return m_duration; }

    /**
     * 请求提取指定位置的缩略图（异步）
     * @param positionMs 位置（毫秒）
     * 提取完成后发出 thumbnailReady 信号
     */
    void requestThumbnail(qint64 positionMs);

    /**
     * 设置缩略图尺寸
     * @param width 宽度（高度自动计算保持比例）
     */
    void setThumbnailWidth(int width) { m_thumbnailWidth = width; }

signals:
    /**
     * 缩略图提取完成信号
     * @param image 提取的缩略图
     * @param positionMs 对应的位置
     */
    void thumbnailReady(const QImage &image, qint64 positionMs);

private slots:
    void doExtract(qint64 positionMs);

private:
    /**
     * 提取指定位置的帧
     * @param positionMs 位置（毫秒）
     * @return 提取的图像，失败返回空图像
     */
    QImage extractFrame(qint64 positionMs);

    /**
     * 将 AVFrame 转换为 QImage
     */
    QImage frameToImage(AVFrame *frame);
    
    /**
     * 配置硬件加速（参考 FFPlayer::configure_hwaccel）
     * @return 是否成功配置硬件加速
     */
    bool configureHWAccel(const AVCodec *codec);
    
    /**
     * 将硬件帧转换为软件帧
     * @return 0 成功，<0 失败
     */
    int hwFrameToSw(AVFrame *hwFrame, AVFrame *swFrame);
    
public:
    /**
     * 获取 FFmpeg 硬件设备类型（公开以便辅助函数使用）
     */
    static AVHWDeviceType getAVHWDeviceType(ThumbnailHWAccelType type);
    
private:

    // FFmpeg 上下文
    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;
    int m_videoStreamIndex = -1;
    
    // 硬件加速
    ThumbnailHWAccelType m_hwAccelType = ThumbnailHWAccelType::None;
    AVBufferRef *m_hwDeviceCtx = nullptr;
    AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
    bool m_hwAccelFailed = false;

    // 视频信息
    qint64 m_duration = 0;  // 毫秒
    int m_videoWidth = 0;
    int m_videoHeight = 0;

    // 缩略图设置
    int m_thumbnailWidth = 320;

    // 缓存（位置 -> 图像）
    QCache<qint64, QImage> m_cache;
    
    // 正在提取的位置（避免重复请求）
    QSet<qint64> m_pendingPositions;
    
    // 线程安全
    QMutex m_mutex;
    
    // 工作线程
    QThread *m_workerThread = nullptr;
    
    // 当前文件路径
    QString m_filePath;
};

#endif // THUMBNAILEXTRACTOR_H

