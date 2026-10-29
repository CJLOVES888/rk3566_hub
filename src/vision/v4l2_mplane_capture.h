/*
 * ============================================================================
 * v4l2_mplane_capture.h —— Rockchip CSI：V4L2 multiplanar 取 NV12 → BGR
 * ============================================================================
 */

#pragma once

#include <QString>
#include <opencv2/core.hpp>

namespace vision {

class V4l2MplaneCapture {
public:
    V4l2MplaneCapture() = default;
    ~V4l2MplaneCapture() { close(); }

    V4l2MplaneCapture(const V4l2MplaneCapture &) = delete;
    V4l2MplaneCapture &operator=(const V4l2MplaneCapture &) = delete;

    /** 打开设备并协商 NV12；成功返回 true */
    bool open(const QString &device, int width, int height, QString *err = nullptr);
    void close();
    bool isOpen() const { return m_fd >= 0; }

    /** 读一帧，输出 BGR888 Mat（连续） */
    bool readBgr(cv::Mat *bgrOut, QString *err = nullptr);

    int width() const { return m_width; }
    int height() const { return m_height; }
    int stride() const { return m_stride; }

private:
    struct Buffer {
        void *start[2] = {nullptr, nullptr};
        size_t length[2] = {0, 0};
        int nPlanes = 0;
    };

    bool initMmap(QString *err);
    bool queueAll(QString *err);
    bool yuvToBgr(const uchar *y, int yStride, const uchar *uv, int uvStride,
                  cv::Mat *bgrOut) const;

    int m_fd = -1;
    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;
    bool m_mplane = true;
    bool m_nv21 = false;
    int m_bufType = 0;
    Buffer m_bufs[4];
    unsigned m_nbufs = 0;
};

} // namespace vision
