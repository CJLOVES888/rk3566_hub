/*
 * ============================================================================
 * camera_worker.h —— Qt 封装：独立线程调用 hub_v4l2_capture
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
    int m_preferredIndex;
    int m_width;
    int m_height;
    std::atomic<bool> m_running{false};
};

} // namespace vision
