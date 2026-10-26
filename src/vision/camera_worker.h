/*
 * ============================================================================
 * camera_worker.h —— 摄像头采集（兼容 USB UVC + Rockchip CSI）
 * ----------------------------------------------------------------------------
 * RK3566 CSI（树莓派排线摄像头）常走 multiplanar V4L2，OpenCV 仅 CAP_V4L2
 * 打开 /dev/video0 可能失败。本类策略：
 *   1) 环境变量 HUB_CAMERA_DEVICE（如 /dev/video-camera0 或 11）
 *   2) 优先打开 /dev/video-camera0
 *   3) 扫描 /dev/video0..15，能读出一帧的才用
 *   4) V4L2 失败则尝试 GStreamer(v4l2src) 管道
 * ============================================================================
 */

#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <atomic>

namespace vision {

class CameraWorker : public QObject {
    Q_OBJECT

public:
    explicit CameraWorker(int preferredIndex = -1,
                          int frameWidth = 640,
                          int frameHeight = 480,
                          QObject *parent = nullptr);

    void stop();

public slots:
    void start();

signals:
    void frameReady(const QImage &frame);
    void fpsChanged(double fps);
    void errorOccurred(const QString &message);

private:
    int m_preferredIndex;   // <0 表示自动探测
    int m_width;
    int m_height;
    std::atomic<bool> m_running{false};
};

} // namespace vision
