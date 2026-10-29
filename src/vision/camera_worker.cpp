/*
 * ============================================================================
 * camera_worker.cpp —— CSI / UVC 采集
 * ----------------------------------------------------------------------------
 * Rockchip CSI 常见坑：GStreamer/OpenCV 报 640x480x3，实际仍是 NV12，
 * 按 BGR 显示会双影+发绿。策略：
 *   1) 优先让 videoconvert 真正输出 RGB/BGR（并走 RgbMat/BgrMat）
 *   2) 其次取原始 NV12 单平面（rows=H*3/2, ch=1）再 cvtColor
 *   3) 不要用「假 BGR」指针硬解 NV12（stride 不对会更花）
 * ============================================================================
 */

#include "camera_worker.h"
#include "v4l2_mplane_capture.h"

#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

namespace vision {

namespace {

enum class PixelPath {
    BgrMat,
    RgbMat,
    Nv12,
    Nv21,
    Yuyv2,
    Gray,
};

bool tryGrabOne(cv::VideoCapture &cap, cv::Mat *out)
{
    cv::Mat frame;
    for (int i = 0; i < 12; ++i) {
        if (!cap.read(frame)) {
            QThread::msleep(40);
            continue;
        }
        if (!frame.empty()) {
            if (out) {
                *out = frame;
            }
            return true;
        }
        QThread::msleep(40);
    }
    return false;
}

QString fourccToString(int fourcc)
{
    char s[5] = {
        char(fourcc & 0xFF),
        char((fourcc >> 8) & 0xFF),
        char((fourcc >> 16) & 0xFF),
        char((fourcc >> 24) & 0xFF),
        0};
    return QString::fromLatin1(s);
}

bool isRealColorMat(const cv::Mat &m, int expectW, int expectH)
{
    // 真彩色：尺寸正确且通道 3；NV12 假 BGR 也常满足，需结合打开方式判断
    return m.channels() == 3 && m.cols == expectW && m.rows == expectH;
}

bool isNv12Plane(const cv::Mat &m, int expectW, int expectH)
{
    if (m.channels() != 1 || expectW <= 0 || expectH <= 0) {
        return false;
    }
    return m.cols == expectW && m.rows == expectH * 3 / 2;
}

PixelPath detectPixelPath(const cv::Mat &m, int expectW, int expectH)
{
    if (isNv12Plane(m, expectW, expectH)) {
        return PixelPath::Nv12;
    }
    if (m.channels() == 1) {
        if (m.rows * 2 % 3 == 0 && m.cols > 0) {
            return PixelPath::Nv12;
        }
        return PixelPath::Gray;
    }
    if (m.channels() == 2) {
        return PixelPath::Yuyv2;
    }
    return PixelPath::BgrMat;
}

bool yuv420spToRgb(const cv::Mat &src, int expectW, int expectH, bool nv21, cv::Mat *rgbOut)
{
    const int w = expectW > 0 ? expectW : src.cols;
    const int h = expectH > 0 ? expectH
                               : (src.channels() == 1 ? src.rows * 2 / 3 : src.rows);
    if (w <= 0 || h <= 0) {
        return false;
    }

    const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 3 / 2;
    cv::Mat plane;

    if (src.channels() == 1 && src.cols == w && src.rows == h * 3 / 2) {
        plane = src;
    } else if (src.channels() == 1 && src.isContinuous()
               && src.total() * src.elemSize() >= need) {
        plane = cv::Mat(h * 3 / 2, w, CV_8UC1, const_cast<uchar *>(src.ptr()));
    } else if (src.channels() == 3 && src.cols == w && src.rows == h) {
        // Rockchip CSI 典型「假 BGR」：头是 640x480x3，内存前段仍是紧凑 NV12
        const size_t have = src.isContinuous()
                                ? (src.total() * src.elemSize())
                                : (static_cast<size_t>(src.rows) * src.step);
        if (have < need) {
            return false;
        }
        plane = cv::Mat(h * 3 / 2, w, CV_8UC1, const_cast<uchar *>(src.ptr()));
    } else {
        return false;
    }

    cv::Mat bgr;
    cv::cvtColor(plane, bgr, nv21 ? cv::COLOR_YUV2BGR_NV21 : cv::COLOR_YUV2BGR_NV12);
    if (bgr.empty()) {
        return false;
    }
    cv::cvtColor(bgr, *rgbOut, cv::COLOR_BGR2RGB);
    return !rgbOut->empty();
}

bool toRgb888(const cv::Mat &src, PixelPath path, int expectW, int expectH, cv::Mat *rgbOut)
{
    if (!rgbOut || src.empty()) {
        return false;
    }

    switch (path) {
    case PixelPath::Nv12:
        return yuv420spToRgb(src, expectW, expectH, /*nv21=*/false, rgbOut);
    case PixelPath::Nv21:
        return yuv420spToRgb(src, expectW, expectH, /*nv21=*/true, rgbOut);
    case PixelPath::Yuyv2: {
        cv::Mat in = src;
        if (src.channels() != 2) {
            return false;
        }
        cv::Mat bgr;
        cv::cvtColor(in, bgr, cv::COLOR_YUV2BGR_YUY2);
        cv::cvtColor(bgr, *rgbOut, cv::COLOR_BGR2RGB);
        return !rgbOut->empty();
    }
    case PixelPath::Gray:
        cv::cvtColor(src, *rgbOut, cv::COLOR_GRAY2RGB);
        return !rgbOut->empty();
    case PixelPath::RgbMat:
        if (src.channels() == 3) {
            *rgbOut = src.clone();
            return true;
        }
        return false;
    case PixelPath::BgrMat:
    default:
        if (src.channels() == 3) {
            cv::cvtColor(src, *rgbOut, cv::COLOR_BGR2RGB);
            return !rgbOut->empty();
        }
        if (src.channels() == 4) {
            cv::cvtColor(src, *rgbOut, cv::COLOR_BGRA2RGB);
            return !rgbOut->empty();
        }
        return false;
    }
}

void safeRelease(cv::VideoCapture &cap)
{
    if (cap.isOpened()) {
        cap.release();
    }
    QThread::msleep(200);
}

void configureV4l2(cv::VideoCapture &cap, int width, int height, int fourcc, bool convertRgb)
{
    if (fourcc != 0) {
        cap.set(cv::CAP_PROP_FOURCC, fourcc);
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap.set(cv::CAP_PROP_FPS, 30);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    cap.set(cv::CAP_PROP_CONVERT_RGB, convertRgb ? 1 : 0);
}

bool openV4l2(cv::VideoCapture &cap, const QString &path, int width, int height,
              PixelPath *outPath)
{
    struct Try {
        int fourcc;
        bool convertRgb;
    };
    const Try tries[] = {
        {cv::VideoWriter::fourcc('N', 'V', '1', '2'), false},
        {cv::VideoWriter::fourcc('N', 'V', '2', '1'), false},
        {cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V'), false},
        {cv::VideoWriter::fourcc('N', 'V', '1', '2'), true},
        {0, true},
    };

    for (const Try &t : tries) {
        safeRelease(cap);
        if (!cap.open(path.toStdString(), cv::CAP_V4L2)) {
            continue;
        }
        configureV4l2(cap, width, height, t.fourcc, t.convertRgb);
        cv::Mat probe;
        if (!tryGrabOne(cap, &probe)) {
            safeRelease(cap);
            continue;
        }
        if (!t.convertRgb && isNv12Plane(probe, width, height)) {
            *outPath = (t.fourcc == cv::VideoWriter::fourcc('N', 'V', '2', '1'))
                           ? PixelPath::Nv21
                           : PixelPath::Nv12;
        } else if (t.convertRgb && isRealColorMat(probe, width, height)) {
            *outPath = PixelPath::BgrMat;
        } else {
            *outPath = detectPixelPath(probe, width, height);
        }
        qInfo() << "Camera OK V4L2" << path
                << "fourcc=" << fourccToString(static_cast<int>(cap.get(cv::CAP_PROP_FOURCC)))
                << "convertRgb=" << t.convertRgb
                << "mat=" << probe.cols << "x" << probe.rows << "ch=" << probe.channels()
                << "path=" << static_cast<int>(*outPath);
        return true;
    }
    return false;
}

bool openGst(cv::VideoCapture &cap, const QString &device, int width, int height,
             PixelPath *outPath)
{
    const bool csiLike = device.contains(QStringLiteral("camera"));

    // CSI 上 videoconvert→BGR/RGB 经常「假成功」（仍是 NV12），一律按 Nv12 解。
    const QStringList pipes = {
        QStringLiteral(
            "v4l2src device=%1 ! videoconvert ! "
            "video/x-raw,format=BGR,width=%2,height=%3 ! "
            "appsink drop=true max-buffers=1 sync=false")
            .arg(device)
            .arg(width)
            .arg(height),
        QStringLiteral(
            "v4l2src device=%1 io-mode=2 ! "
            "video/x-raw,format=NV12,width=%2,height=%3 ! "
            "appsink drop=true max-buffers=1 sync=false")
            .arg(device)
            .arg(width)
            .arg(height),
        QStringLiteral(
            "v4l2src device=%1 ! "
            "video/x-raw,format=NV12,width=%2,height=%3 ! "
            "appsink drop=true max-buffers=1 sync=false")
            .arg(device)
            .arg(width)
            .arg(height),
        QStringLiteral(
            "v4l2src device=%1 ! videoconvert ! video/x-raw,format=BGR ! "
            "appsink drop=true max-buffers=1 sync=false")
            .arg(device),
    };

    for (int i = 0; i < pipes.size(); ++i) {
        safeRelease(cap);
        if (!cap.open(pipes.at(i).toStdString(), cv::CAP_GSTREAMER)) {
            continue;
        }
        cv::Mat probe;
        if (!tryGrabOne(cap, &probe)) {
            safeRelease(cap);
            continue;
        }

        if (isNv12Plane(probe, width, height)) {
            *outPath = PixelPath::Nv12;
        } else if (csiLike && isRealColorMat(probe, width, height)) {
            // 假 BGR：必须走 NV12 解码，不能当真彩色
            *outPath = PixelPath::Nv12;
        } else if (isRealColorMat(probe, width, height)) {
            *outPath = PixelPath::BgrMat;
        } else {
            safeRelease(cap);
            continue;
        }

        qInfo() << "Camera OK GST" << device
                << "pipe=" << i
                << "mat=" << probe.cols << "x" << probe.rows << "ch=" << probe.channels()
                << "path=" << static_cast<int>(*outPath)
                << (csiLike && *outPath == PixelPath::Nv12 ? "(CSI→NV12 decode)" : "");
        return true;
    }
    return false;
}

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

    // -------- 路径 A：原生 V4L2 MPLANE（CSI 首选，颜色正确）--------
    V4l2MplaneCapture native;
    for (const QString &dev : devices) {
        if (!m_running.load()) {
            return;
        }
        if (!dev.startsWith(QLatin1Char('/'))) {
            continue;
        }
        QString err;
        if (native.open(dev, m_width, m_height, &err)) {
            qInfo() << "Camera using:" << dev << "(v4l2-mplane native)"
                    << native.width() << "x" << native.height()
                    << "stride=" << native.stride();

            QElapsedTimer fpsTimer;
            fpsTimer.start();
            qint64 lastFpsMark = 0;
            int frameCount = 0;
            bool loggedOnce = false;

            while (m_running.load()) {
                cv::Mat bgr;
                QString readErr;
                if (!native.readBgr(&bgr, &readErr) || bgr.empty()) {
                    QThread::msleep(30);
                    continue;
                }
                if (!loggedOnce) {
                    qInfo() << "First native BGR frame" << bgr.cols << "x" << bgr.rows
                            << "ch=" << bgr.channels();
                    loggedOnce = true;
                }
                cv::Mat rgb;
                cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
                QImage img(rgb.data, rgb.cols, rgb.rows,
                           static_cast<qsizetype>(rgb.step), QImage::Format_RGB888);
                emit frameReady(img.copy());

                ++frameCount;
                const qint64 now = fpsTimer.elapsed();
                if (now - lastFpsMark >= 1000) {
                    emit fpsChanged(frameCount * 1000.0 / (now - lastFpsMark));
                    frameCount = 0;
                    lastFpsMark = now;
                }
            }
            native.close();
            qDebug() << "CameraWorker native loop exited";
            return;
        }
        qWarning() << "native V4L2 open failed" << dev << err;
    }

    // -------- 路径 B：OpenCV GST / V4L2 回退 --------
    cv::VideoCapture cap;
    bool opened = false;
    QString usedDevice;
    PixelPath pixelPath = PixelPath::BgrMat;

    for (const QString &dev : devices) {
        if (!m_running.load()) {
            return;
        }
        if (!dev.startsWith(QLatin1Char('/'))) {
            continue;
        }
        if (openGst(cap, dev, m_width, m_height, &pixelPath)) {
            opened = true;
            usedDevice = dev + QStringLiteral(" (gst)");
            break;
        }
        if (openV4l2(cap, dev, m_width, m_height, &pixelPath)) {
            opened = true;
            usedDevice = dev + QStringLiteral(" (v4l2)");
            break;
        }
    }

    if (!opened) {
        emit errorOccurred(QStringLiteral("无法打开摄像头，请运行 scripts/probe_camera.sh"));
        return;
    }

    if (usedDevice.contains(QStringLiteral("camera"))) {
        pixelPath = PixelPath::Nv12;
    }

    qInfo() << "Camera using:" << usedDevice
            << "expect=" << m_width << "x" << m_height
            << "pixelPath=" << static_cast<int>(pixelPath);

    QElapsedTimer fpsTimer;
    fpsTimer.start();
    qint64 lastFpsMark = 0;
    int frameCount = 0;
    bool loggedOnce = false;
    cv::Mat frameRaw;

    while (m_running.load()) {
        if (!cap.read(frameRaw) || frameRaw.empty()) {
            emit errorOccurred(QStringLiteral("取帧失败，重试中..."));
            QThread::msleep(200);
            continue;
        }

        if (!loggedOnce) {
            qInfo() << "First frame mat=" << frameRaw.cols << "x" << frameRaw.rows
                    << "ch=" << frameRaw.channels() << "type=" << frameRaw.type();
            loggedOnce = true;
            qInfo() << "Resolved pixelPath=" << static_cast<int>(pixelPath);
        }

        cv::Mat frameRgb;
        if (!toRgb888(frameRaw, pixelPath, m_width, m_height, &frameRgb) || frameRgb.empty()) {
            continue;
        }

        QImage img(frameRgb.data,
                   frameRgb.cols,
                   frameRgb.rows,
                   static_cast<qsizetype>(frameRgb.step),
                   QImage::Format_RGB888);
        emit frameReady(img.copy());

        ++frameCount;
        const qint64 now = fpsTimer.elapsed();
        if (now - lastFpsMark >= 1000) {
            emit fpsChanged(frameCount * 1000.0 / (now - lastFpsMark));
            frameCount = 0;
            lastFpsMark = now;
        }
    }

    cap.release();
    qDebug() << "CameraWorker loop exited";
}

} // namespace vision
