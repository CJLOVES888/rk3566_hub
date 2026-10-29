/*
 * ============================================================================
 * v4l2_mplane_capture.cpp
 * ============================================================================
 */

#include "v4l2_mplane_capture.h"

#include <QDebug>

#include <opencv2/imgproc.hpp>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include <vector>

namespace vision {

namespace {

int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do {
        r = ::ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

} // namespace

bool V4l2MplaneCapture::open(const QString &device, int width, int height, QString *err)
{
    close();
    m_width = width;
    m_height = height;
    m_nv21 = (qgetenv("HUB_CAMERA_YUV") == QByteArray("nv21"));

    m_fd = ::open(device.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK, 0);
    if (m_fd < 0) {
        if (err) {
            *err = QStringLiteral("open %1 failed: %2").arg(device, QString::fromLocal8Bit(strerror(errno)));
        }
        return false;
    }

    v4l2_capability cap{};
    if (xioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        if (err) {
            *err = QStringLiteral("VIDIOC_QUERYCAP failed");
        }
        close();
        return false;
    }

    const bool canMplane = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    const bool canSingle = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
    if (!canMplane && !canSingle) {
        if (err) {
            *err = QStringLiteral("device has no VIDEO_CAPTURE(_MPLANE)");
        }
        close();
        return false;
    }

    m_mplane = canMplane;
    m_bufType = m_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                         : V4L2_BUF_TYPE_VIDEO_CAPTURE;

    v4l2_format fmt{};
    fmt.type = m_bufType;
    if (m_mplane) {
        fmt.fmt.pix_mp.width = static_cast<__u32>(width);
        fmt.fmt.pix_mp.height = static_cast<__u32>(height);
        fmt.fmt.pix_mp.pixelformat = m_nv21 ? V4L2_PIX_FMT_NV21 : V4L2_PIX_FMT_NV12;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes = 1;
    } else {
        fmt.fmt.pix.width = static_cast<__u32>(width);
        fmt.fmt.pix.height = static_cast<__u32>(height);
        fmt.fmt.pix.pixelformat = m_nv21 ? V4L2_PIX_FMT_NV21 : V4L2_PIX_FMT_NV12;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }

    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
        // 再试两平面
        if (m_mplane) {
            fmt.fmt.pix_mp.num_planes = 2;
            if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
                if (err) {
                    *err = QStringLiteral("VIDIOC_S_FMT NV12 failed: %1")
                               .arg(QString::fromLocal8Bit(strerror(errno)));
                }
                close();
                return false;
            }
        } else {
            if (err) {
                *err = QStringLiteral("VIDIOC_S_FMT NV12 failed: %1")
                           .arg(QString::fromLocal8Bit(strerror(errno)));
            }
            close();
            return false;
        }
    }

    if (m_mplane) {
        m_width = static_cast<int>(fmt.fmt.pix_mp.width);
        m_height = static_cast<int>(fmt.fmt.pix_mp.height);
        m_stride = static_cast<int>(fmt.fmt.pix_mp.plane_fmt[0].bytesperline);
        if (m_stride <= 0) {
            m_stride = m_width;
        }
        qInfo() << "V4L2-MPLANE" << device << m_width << "x" << m_height
                << "stride=" << m_stride
                << "planes=" << fmt.fmt.pix_mp.num_planes
                << "fmt=" << (m_nv21 ? "NV21" : "NV12");
    } else {
        m_width = static_cast<int>(fmt.fmt.pix.width);
        m_height = static_cast<int>(fmt.fmt.pix.height);
        m_stride = static_cast<int>(fmt.fmt.pix.bytesperline);
        if (m_stride <= 0) {
            m_stride = m_width;
        }
        qInfo() << "V4L2-CAP" << device << m_width << "x" << m_height
                << "stride=" << m_stride << "fmt=" << (m_nv21 ? "NV21" : "NV12");
    }

    if (!initMmap(err) || !queueAll(err)) {
        close();
        return false;
    }

    int type = m_bufType;
    if (xioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        if (err) {
            *err = QStringLiteral("VIDIOC_STREAMON failed: %1")
                       .arg(QString::fromLocal8Bit(strerror(errno)));
        }
        close();
        return false;
    }
    return true;
}

bool V4l2MplaneCapture::initMmap(QString *err)
{
    v4l2_requestbuffers req{};
    req.count = 3;
    req.type = m_bufType;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        if (err) {
            *err = QStringLiteral("VIDIOC_REQBUFS failed");
        }
        return false;
    }
    m_nbufs = req.count;
    if (m_nbufs > 4) {
        m_nbufs = 4;
    }

    for (unsigned i = 0; i < m_nbufs; ++i) {
        v4l2_buffer buf{};
        buf.type = m_bufType;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        v4l2_plane planes[2]{};
        if (m_mplane) {
            buf.m.planes = planes;
            buf.length = 2;
        }
        if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            if (err) {
                *err = QStringLiteral("VIDIOC_QUERYBUF failed");
            }
            return false;
        }

        Buffer &b = m_bufs[i];
        if (m_mplane) {
            b.nPlanes = static_cast<int>(buf.length);
            if (b.nPlanes < 1) {
                b.nPlanes = 1;
            }
            for (int p = 0; p < b.nPlanes; ++p) {
                b.length[p] = planes[p].length;
                b.start[p] = ::mmap(nullptr, planes[p].length, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, m_fd, planes[p].m.mem_offset);
                if (b.start[p] == MAP_FAILED) {
                    b.start[p] = nullptr;
                    if (err) {
                        *err = QStringLiteral("mmap plane failed");
                    }
                    return false;
                }
            }
        } else {
            b.nPlanes = 1;
            b.length[0] = buf.length;
            b.start[0] = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                m_fd, buf.m.offset);
            if (b.start[0] == MAP_FAILED) {
                b.start[0] = nullptr;
                if (err) {
                    *err = QStringLiteral("mmap failed");
                }
                return false;
            }
        }
    }
    return true;
}

bool V4l2MplaneCapture::queueAll(QString *err)
{
    for (unsigned i = 0; i < m_nbufs; ++i) {
        v4l2_buffer buf{};
        buf.type = m_bufType;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        v4l2_plane planes[2]{};
        if (m_mplane) {
            buf.m.planes = planes;
            buf.length = static_cast<__u32>(m_bufs[i].nPlanes);
        }
        if (xioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
            if (err) {
                *err = QStringLiteral("VIDIOC_QBUF failed");
            }
            return false;
        }
    }
    return true;
}

bool V4l2MplaneCapture::yuvToBgr(const uchar *y, int yStride, const uchar *uv, int uvStride,
                                 cv::Mat *bgrOut) const
{
    // 拼成紧凑 NV12/NV21 再转，避免 stride 问题
    std::vector<uchar> packed(static_cast<size_t>(m_width) * m_height * 3 / 2);
    uchar *dst = packed.data();
    for (int row = 0; row < m_height; ++row) {
        memcpy(dst + row * m_width, y + row * yStride, static_cast<size_t>(m_width));
    }
    uchar *dstUv = dst + m_width * m_height;
    const int uvRows = m_height / 2;
    for (int row = 0; row < uvRows; ++row) {
        memcpy(dstUv + row * m_width, uv + row * uvStride, static_cast<size_t>(m_width));
    }

    cv::Mat plane(m_height * 3 / 2, m_width, CV_8UC1, packed.data());
    cv::cvtColor(plane, *bgrOut, m_nv21 ? cv::COLOR_YUV2BGR_NV21 : cv::COLOR_YUV2BGR_NV12);
    return bgrOut && !bgrOut->empty();
}

bool V4l2MplaneCapture::readBgr(cv::Mat *bgrOut, QString *err)
{
    if (m_fd < 0 || !bgrOut) {
        return false;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_fd, &fds);
    timeval tv{};
    tv.tv_sec = 2;
    const int r = ::select(m_fd + 1, &fds, nullptr, nullptr, &tv);
    if (r <= 0) {
        if (err) {
            *err = QStringLiteral("select timeout/error");
        }
        return false;
    }

    v4l2_buffer buf{};
    buf.type = m_bufType;
    buf.memory = V4L2_MEMORY_MMAP;
    v4l2_plane planes[2]{};
    if (m_mplane) {
        buf.m.planes = planes;
        buf.length = 2;
    }
    if (xioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) {
        if (err) {
            *err = QStringLiteral("VIDIOC_DQBUF failed");
        }
        return false;
    }

    const Buffer &b = m_bufs[buf.index];
    bool ok = false;

    if (m_mplane && b.nPlanes >= 2 && b.start[0] && b.start[1]) {
        const int yStride = m_stride;
        const int uvStride = m_stride; // 多数 Rockchip NV12 UV 与 Y 同 stride
        ok = yuvToBgr(static_cast<const uchar *>(b.start[0]), yStride,
                      static_cast<const uchar *>(b.start[1]), uvStride, bgrOut);
    } else if (b.start[0]) {
        // 单平面：紧凑或带 stride 的 NV12
        const uchar *base = static_cast<const uchar *>(b.start[0]);
        if (m_stride == m_width) {
            cv::Mat plane(m_height * 3 / 2, m_width, CV_8UC1, const_cast<uchar *>(base));
            cv::cvtColor(plane, *bgrOut,
                         m_nv21 ? cv::COLOR_YUV2BGR_NV21 : cv::COLOR_YUV2BGR_NV12);
            ok = !bgrOut->empty();
        } else {
            ok = yuvToBgr(base, m_stride, base + m_stride * m_height, m_stride, bgrOut);
        }
    }

    if (xioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
        if (err) {
            *err = QStringLiteral("VIDIOC_QBUF requeue failed");
        }
        return false;
    }
    return ok;
}

void V4l2MplaneCapture::close()
{
    if (m_fd >= 0) {
        int type = m_bufType;
        xioctl(m_fd, VIDIOC_STREAMOFF, &type);
    }
    for (unsigned i = 0; i < m_nbufs; ++i) {
        for (int p = 0; p < m_bufs[i].nPlanes; ++p) {
            if (m_bufs[i].start[p] && m_bufs[i].start[p] != MAP_FAILED) {
                ::munmap(m_bufs[i].start[p], m_bufs[i].length[p]);
            }
            m_bufs[i].start[p] = nullptr;
            m_bufs[i].length[p] = 0;
        }
        m_bufs[i].nPlanes = 0;
    }
    m_nbufs = 0;
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

} // namespace vision
