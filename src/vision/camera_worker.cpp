/*
 * ============================================================================
 * camera_worker.cpp —— CSI / UVC 通用采集（修复 NV12 被当成 BGR 导致“上下一分为二”）
 * ============================================================================
 */

#include "camera_worker.h"

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
    BgrMat,     // Mat 已是 BGR 三通道（GStreamer videoconvert 后）
    Nv12,       // 单通道紧凑 NV12，rows = H*3/2
    Yuyv2,      // 两通道 YUYV
    Gray,
};

bool tryGrabOne(cv::VideoCapture &cap, cv::Mat *out)
{
    cv::Mat frame;
    for (int i = 0; i < 10; ++i) {
        if (!cap.read(frame)) {
            QThread::msleep(30);
            continue;
        }
        if (!frame.empty()) {
            if (out) {
                *out = frame;
            }
            return true;
        }
        QThread::msleep(30);
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

PixelPath detectPixelPath(const cv::Mat &m, int expectW, int expectH)
{
    if (m.channels() == 1) {
        // NV12: H*3/2 行
        if (m.cols == expectW && m.rows == expectH * 3 / 2) {
            return PixelPath::Nv12;
        }
        if (m.rows * 2 % 3 == 0 && m.cols > 0) {
            return PixelPath::Nv12;
        }
        return PixelPath::Gray;
    }
    if (m.channels() == 2) {
        return PixelPath::Yuyv2;
    }
    // 三通道但高度是 1.5 倍：几乎肯定是 NV12 被错误 reshape
    if (m.channels() == 3 && expectH > 0 && m.rows == expectH * 3 / 2 && m.cols == expectW) {
        return PixelPath::Nv12;
    }
    // 三通道高度翻倍：常见“假 BGR”把 NV12 当 8UC3 解读
    if (m.channels() == 3 && expectH > 0 && m.rows == expectH * 2 && m.cols == expectW) {
        return PixelPath::Nv12;
    }
    return PixelPath::BgrMat;
}

bool toRgb888(const cv::Mat &src, PixelPath path, int expectW, int expectH, cv::Mat *rgbOut)
{
    if (!rgbOut || src.empty()) {
        return false;
    }

    cv::Mat bgr;
    switch (path) {
    case PixelPath::Nv12: {
        int w = src.cols;
        int h = expectH > 0 ? expectH : (src.rows * 2 / 3);
        if (src.channels() == 1) {
            if (src.rows == h * 3 / 2) {
                cv::cvtColor(src, bgr, cv::COLOR_YUV2BGR_NV12);
            } else {
                cv::Mat packed(h * 3 / 2, w, CV_8UC1, const_cast<uchar *>(src.ptr()));
                cv::cvtColor(packed, bgr, cv::COLOR_YUV2BGR_NV12);
            }
        } else {
            // 被错误读成 HxW 多通道：用底层指针按 NV12 重释
            const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 3 / 2;
            if (src.total() * src.elemSize() < need) {
                return false;
            }
            cv::Mat packed(h * 3 / 2, w, CV_8UC1, const_cast<uchar *>(src.ptr()));
            cv::cvtColor(packed, bgr, cv::COLOR_YUV2BGR_NV12);
        }
        break;
    }
    case PixelPath::Yuyv2: {
        cv::Mat in = src;
        if (src.channels() != 2) {
            in = cv::Mat(src.rows, src.cols, CV_8UC2, const_cast<uchar *>(src.ptr()), src.step);
        }
        cv::cvtColor(in, bgr, cv::COLOR_YUV2BGR_YUY2);
        break;
    }
    case PixelPath::Gray:
        cv::cvtColor(src, *rgbOut, cv::COLOR_GRAY2RGB);
        return true;
    case PixelPath::BgrMat:
    default:
        if (src.channels() == 3) {
            cv::cvtColor(src, *rgbOut, cv::COLOR_BGR2RGB);
            return true;
        }
        if (src.channels() == 4) {
            cv::cvtColor(src, *rgbOut, cv::COLOR_BGRA2RGB);
            return true;
        }
        return false;
    }

    if (bgr.empty()) {
        return false;
    }
    cv::cvtColor(bgr, *rgbOut, cv::COLOR_BGR2RGB);
    (void)expectW;
    return !rgbOut->empty();
}

void safeRelease(cv::VideoCapture &cap)
{
    if (cap.isOpened()) {
        cap.release();
    }
    // Rockchip CSI：失败管道释放后设备短暂忙，立刻重开易 OOM / Invalid argument
    QThread::msleep(150);
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
        // OpenCV 自动转 RGB/BGR（部分板子可直接得到正常彩色）
        {cv::VideoWriter::fourcc('N', 'V', '1', '2'), true},
        {cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V'), true},
        {0, true},
        // 原始缓冲 + 自研 NV12/YUYV 解码
        {cv::VideoWriter::fourcc('N', 'V', '1', '2'), false},
        {cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V'), false},
        {0, false},
    };

    for (const Try &t : tries) {
        if (!cap.open(path.toStdString(), cv::CAP_V4L2)) {
            safeRelease(cap);
            continue;
        }
        configureV4l2(cap, width, height, t.fourcc, t.convertRgb);
        cv::Mat probe;
        if (!tryGrabOne(cap, &probe)) {
            safeRelease(cap);
            continue;
        }
        if (t.convertRgb && probe.channels() >= 3) {
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
    // 本板已验证：不锁输入格式的 videoconvert→BGR 能打开并出 640x480x3。
    // 强锁 NV12+framerate 反而会 Failed to allocate required memory。
    // Rockchip 内存紧张时可用 io-mode=4(dmabuf)/2(mmap) 再试 NV12。
    const QStringList pipes = {
        QStringLiteral(
            "v4l2src device=%1 ! videoconvert ! "
            "video/x-raw,format=BGR,width=%2,height=%3 ! "
            "appsink drop=true max-buffers=1 sync=false")
            .arg(device)
            .arg(width)
            .arg(height),
        QStringLiteral(
            "v4l2src device=%1 ! videoconvert ! video/x-raw,format=BGR ! "
            "appsink drop=true max-buffers=1 sync=false")
            .arg(device),
        QStringLiteral(
            "v4l2src device=%1 io-mode=4 ! "
            "video/x-raw,format=NV12,width=%2,height=%3 ! "
            "videoconvert ! video/x-raw,format=BGR ! "
            "appsink drop=true max-buffers=1 sync=false")
            .arg(device)
            .arg(width)
            .arg(height),
        QStringLiteral(
            "v4l2src device=%1 io-mode=2 ! "
            "video/x-raw,format=NV12,width=%2,height=%3 ! "
            "videoconvert ! video/x-raw,format=BGR ! "
            "appsink drop=true max-buffers=1 sync=false")
            .arg(device)
            .arg(width)
            .arg(height),
    };

    for (int pipeIndex = 0; pipeIndex < pipes.size(); ++pipeIndex) {
        const QString &pipe = pipes.at(pipeIndex);
        safeRelease(cap);
        if (!cap.open(pipe.toStdString(), cv::CAP_GSTREAMER)) {
            safeRelease(cap);
            continue;
        }
        cv::Mat probe;
        if (!tryGrabOne(cap, &probe)) {
            safeRelease(cap);
            continue;
        }
        if (probe.channels() == 3 && probe.rows == height &&
            (probe.cols == width || probe.cols > 0)) {
            *outPath = PixelPath::BgrMat;
        } else {
            *outPath = detectPixelPath(probe, width, height);
        }
        qInfo() << "Camera OK GST" << device
                << "pipe=" << pipeIndex
                << "mat=" << probe.cols << "x" << probe.rows << "ch=" << probe.channels()
                << "path=" << static_cast<int>(*outPath);
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
    // 已指定 HUB_CAMERA_DEVICE 时不再盲目扫全部节点，减少无效打开警告
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

    cv::VideoCapture cap;
    bool opened = false;
    QString usedDevice;
    PixelPath pixelPath = PixelPath::BgrMat;

    const QStringList devices = candidateDevices(m_preferredIndex);
    qInfo() << "Camera probe candidates:" << devices;

    // 优先 GST（本板 CSI 实测可通），失败再 V4L2（含自动 convert + NV12 自解）
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
            // 用真实首帧再校正一次路径
            pixelPath = detectPixelPath(frameRaw, m_width, m_height);
            if (frameRaw.channels() == 3 && frameRaw.cols == m_width && frameRaw.rows == m_height
                && usedDevice.contains(QStringLiteral("gst"))) {
                pixelPath = PixelPath::BgrMat;
            }
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
