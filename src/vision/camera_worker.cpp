/*
 * ============================================================================
 * camera_worker.cpp —— Qt 线程循环采集（hub_v4l2_capture）
 * ============================================================================
 */

#include "camera_worker.h"
#include "hub_v4l2_capture.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QThread>
#include <QDebug>

#include <vector>

namespace vision {

namespace {

QStringList candidateDevices(int preferredIndex)
{
    QStringList list;
    auto addUnique = [&list](const QString &p) {
        if (!p.isEmpty() && !list.contains(p)) {
            list << p;
        }
    };

    const QString fromEnv =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("HUB_CAMERA_DEVICE")).trimmed();
    addUnique(fromEnv);
    if (QFileInfo::exists(QStringLiteral("/dev/video-camera0"))) {
        addUnique(QStringLiteral("/dev/video-camera0"));
    }
    if (preferredIndex >= 0) {
        addUnique(QStringLiteral("/dev/video%1").arg(preferredIndex));
    }
    if (fromEnv.isEmpty()) {
        for (int i = 0; i <= 15; ++i) {
            const QString p = QStringLiteral("/dev/video%1").arg(i);
            if (QFileInfo::exists(p)) {
                addUnique(p);
            }
        }
    }
    return list;
}

} // namespace

CameraWorker::CameraWorker(int preferredIndex, int frameWidth, int frameHeight, QObject *parent)
    : QObject(parent)
    , m_preferredIndex(preferredIndex)
    , m_width(frameWidth)
    , m_height(frameHeight)
{
}

void CameraWorker::stop()
{
    m_running = false;
}

void CameraWorker::start()
{
    m_running = true;
    const QStringList devices = candidateDevices(m_preferredIndex);
    qInfo() << "Camera probe candidates:" << devices;

    for (const QString &dev : devices) {
        if (!m_running.load()) {
            return;
        }
        if (!dev.startsWith(QLatin1Char('/'))) {
            continue;
        }

        char err[256] = {0};
        hub_v4l2_capture_t *cap =
            hub_v4l2_open(dev.toLocal8Bit().constData(), m_width, m_height, err, sizeof(err));
        if (!cap) {
            qWarning() << "hub_v4l2 open failed" << dev << QString::fromLocal8Bit(err);
            continue;
        }

        const int w = hub_v4l2_width(cap);
        const int h = hub_v4l2_height(cap);
        qInfo() << "Camera using:" << dev << "(hub_v4l2)" << w << "x" << h
                << "stride=" << hub_v4l2_stride(cap);

        std::vector<uint8_t> rgb(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
        QElapsedTimer fpsTimer;
        fpsTimer.start();
        qint64 lastFpsMark = 0;
        int frameCount = 0;
        bool loggedOnce = false;

        while (m_running.load()) {
            err[0] = '\0';
            if (!hub_v4l2_read_rgb(cap, rgb.data(), err, sizeof(err))) {
                QThread::msleep(30);
                continue;
            }
            if (!loggedOnce) {
                qInfo() << "First RGB frame" << w << "x" << h;
                loggedOnce = true;
            }

            QImage img(rgb.data(), w, h, w * 3, QImage::Format_RGB888);
            emit frameReady(img.copy());

            ++frameCount;
            const qint64 now = fpsTimer.elapsed();
            if (now - lastFpsMark >= 1000) {
                emit fpsChanged(frameCount * 1000.0 / (now - lastFpsMark));
                frameCount = 0;
                lastFpsMark = now;
            }
        }

        hub_v4l2_close(cap);
        qDebug() << "CameraWorker loop exited";
        return;
    }

    emit errorOccurred(QStringLiteral("无法打开摄像头（C V4L2），请运行 scripts/probe_camera.sh"));
}

} // namespace vision
