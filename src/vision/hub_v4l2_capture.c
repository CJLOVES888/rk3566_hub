/*
 * ============================================================================
 * hub_v4l2_capture.c —— Rockchip CSI / V4L2 MPLANE → RGB888
 * ----------------------------------------------------------------------------
 * 理念摘要（结合 docs 中「采集理念」阅读）：
 *   1) 摄像头是设备文件，用 open + ioctl 与驱动协商
 *   2) mmap 环形缓冲：QBUF 交空缓冲 / DQBUF 取满帧 / 用完再 QBUF
 *   3) 驱动给 NV12(YUV)，本文件转成紧密 RGB888 给上层显示
 * ============================================================================
 */

#include "hub_v4l2_capture.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <linux/videodev2.h>

/* -------------------------------------------------------------------------- */
/* 内部结构：一帧在驱动里可能占 1～2 个 plane（Y / UV）                         */
/* -------------------------------------------------------------------------- */

/** 单个视频缓冲：mmap 后的用户态指针与长度 */
struct hub_buf {
    void *start[2];   /* plane0 / plane1 映射地址 */
    size_t length[2]; /* 各 plane 字节长度 */
    int n_planes;     /* 实际 plane 个数 */
};

/** 采集会话句柄：打开设备后的全部运行状态（对外不透明） */
struct hub_v4l2_capture {
    int fd;              /* /dev/video* 文件描述符 */
    int width;           /* 实际帧宽（以驱动 S_FMT 返回为准） */
    int height;          /* 实际帧高 */
    int stride;          /* 一行字节跨度，可能 > width */
    int mplane;          /* 1= multiplanar 采集 */
    int nv21;            /* 1=NV21，0=NV12（U/V 顺序不同） */
    int buf_type;        /* V4L2_BUF_TYPE_VIDEO_CAPTURE(_MPLANE) */
    struct hub_buf bufs[4]; /* 环形缓冲槽位 */
    unsigned nbufs;      /* 实际申请到的缓冲个数 */
    uint8_t *packed;     /* 去掉 stride 后的紧凑 NV12/NV21 临时区 */
    size_t packed_size;  /* packed 容量 */
};

/* -------------------------------------------------------------------------- */
/* 工具函数                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * set_err —— 把错误原因写入调用方提供的缓冲区（便于上层打印）
 * @param err     输出缓冲，可为 NULL（则什么都不做）
 * @param err_len 缓冲长度
 * @param msg     错误字符串
 */
static void set_err(char *err, size_t err_len, const char *msg)
{
    if (err && err_len > 0) {
        snprintf(err, err_len, "%s", msg ? msg : "");
    }
}

/**
 * xioctl —— 对 ioctl 的薄封装：若被信号打断（EINTR）则自动重试
 * 摄像头控制几乎全靠 ioctl，统一走这里更稳妥。
 */
static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

/**
 * clamp_u8 —— 把整数裁剪到 0～255，供 YUV→RGB 结果写入字节
 */
static int clamp_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return v;
}

/**
 * nv12_to_rgb —— 把「紧凑」NV12/NV21 转成紧密 RGB888
 *
 * 理念：采集用 YUV 省带宽，显示要 RGB；本函数做色彩空间转换。
 * 布局假设：先整幅 Y（w*h），再半高 UV 交错（w*h/2），无行填充。
 * 公式：BT.601 定点近似，避免浮点开销。
 *
 * @param src  紧凑 NV12/NV21 数据
 * @param w,h  分辨率
 * @param nv21 非 0 表示 NV21（V 在前），否则 NV12（U 在前）
 * @param rgb  输出缓冲，至少 w*h*3 字节，每像素 R,G,B
 */
static void nv12_to_rgb(const uint8_t *src, int w, int h, int nv21, uint8_t *rgb)
{
    const uint8_t *y_plane = src;
    const uint8_t *uv_plane = src + w * h;
    int row, col;

    for (row = 0; row < h; ++row) {
        const uint8_t *y_row = y_plane + row * w;
        const uint8_t *uv_row = uv_plane + (row / 2) * w;
        uint8_t *out = rgb + row * w * 3;
        for (col = 0; col < w; ++col) {
            int Y = y_row[col];
            int U, V;
            if (nv21) {
                V = uv_row[(col & ~1)];
                U = uv_row[(col & ~1) + 1];
            } else {
                U = uv_row[(col & ~1)];
                V = uv_row[(col & ~1) + 1];
            }
            U -= 128;
            V -= 128;
            /* 定点近似：1.402≈359/256, 0.344≈88/256, 0.714≈183/256, 1.772≈453/256 */
            int R = Y + ((359 * V) >> 8);
            int G = Y - ((88 * U + 183 * V) >> 8);
            int B = Y + ((453 * U) >> 8);
            out[col * 3 + 0] = (uint8_t)clamp_u8(R);
            out[col * 3 + 1] = (uint8_t)clamp_u8(G);
            out[col * 3 + 2] = (uint8_t)clamp_u8(B);
        }
    }
}

/**
 * pack_nv12 —— 把带 stride 的 Y/UV 平面「打包」成紧凑 NV12/NV21
 *
 * 理念：驱动一行可能有对齐填充（stride > width），直接当紧凑格式解会花屏；
 * 这里按行只拷贝有效 width 字节，得到标准布局再交给 nv12_to_rgb。
 *
 * @param y,y_stride   亮度平面及行跨度
 * @param uv,uv_stride 色度平面及行跨度
 * @param w,h          图像宽高
 * @param dst          输出紧凑缓冲（容量需 >= w*h*3/2）
 */
static void pack_nv12(const uint8_t *y, int y_stride, const uint8_t *uv, int uv_stride,
                      int w, int h, uint8_t *dst)
{
    int row;
    for (row = 0; row < h; ++row) {
        memcpy(dst + row * w, y + row * y_stride, (size_t)w);
    }
    {
        uint8_t *dst_uv = dst + w * h;
        int uv_rows = h / 2;
        for (row = 0; row < uv_rows; ++row) {
            memcpy(dst_uv + row * w, uv + row * uv_stride, (size_t)w);
        }
    }
}

/**
 * ensure_packed —— 确保 packed 临时缓冲已分配且够大
 * @return 1 成功，0 失败（malloc 失败）
 */
static int ensure_packed(hub_v4l2_capture_t *cap)
{
    size_t need = (size_t)cap->width * (size_t)cap->height * 3 / 2;
    if (cap->packed && cap->packed_size >= need) {
        return 1;
    }
    free(cap->packed);
    cap->packed = (uint8_t *)malloc(need);
    cap->packed_size = cap->packed ? need : 0;
    return cap->packed != NULL;
}

/**
 * init_mmap —— 向驱动申请若干视频缓冲，并用 mmap 映射到用户态
 *
 * 理念（环形缓冲的「建槽」阶段）：
 *   VIDIOC_REQBUFS  → 申请 N 块缓冲
 *   VIDIOC_QUERYBUF → 查询每块大小/偏移
 *   mmap            → 映射进本进程，之后 DQBUF 即可直接读像素
 *
 * @return 1 成功，0 失败
 */
static int init_mmap(hub_v4l2_capture_t *cap, char *err, size_t err_len)
{
    struct v4l2_requestbuffers req;
    unsigned i;

    memset(&req, 0, sizeof(req));
    req.count = 3;
    req.type = (uint32_t)cap->buf_type;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(cap->fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        set_err(err, err_len, "VIDIOC_REQBUFS failed");
        return 0;
    }
    cap->nbufs = req.count;
    if (cap->nbufs > 4) {
        cap->nbufs = 4;
    }

    for (i = 0; i < cap->nbufs; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[2];
        struct hub_buf *b = &cap->bufs[i];
        int p;

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = (uint32_t)cap->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (cap->mplane) {
            buf.m.planes = planes;
            buf.length = 2;
        }
        if (xioctl(cap->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            set_err(err, err_len, "VIDIOC_QUERYBUF failed");
            return 0;
        }

        memset(b, 0, sizeof(*b));
        if (cap->mplane) {
            b->n_planes = (int)buf.length;
            if (b->n_planes < 1) {
                b->n_planes = 1;
            }
            for (p = 0; p < b->n_planes; ++p) {
                b->length[p] = planes[p].length;
                b->start[p] = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                   cap->fd, planes[p].m.mem_offset);
                if (b->start[p] == MAP_FAILED) {
                    b->start[p] = NULL;
                    set_err(err, err_len, "mmap plane failed");
                    return 0;
                }
            }
        } else {
            b->n_planes = 1;
            b->length[0] = buf.length;
            b->start[0] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cap->fd,
                               buf.m.offset);
            if (b->start[0] == MAP_FAILED) {
                b->start[0] = NULL;
                set_err(err, err_len, "mmap failed");
                return 0;
            }
        }
    }
    return 1;
}

/**
 * queue_all —— 把所有缓冲通过 VIDIOC_QBUF 交给驱动
 *
 * 理念：开流前必须先有「空槽」；否则驱动无处写帧。
 * 之后取帧循环里：DQBUF 取出 → 用完 → 再 QBUF 归还，形成环形队列。
 *
 * @return 1 成功，0 失败
 */
static int queue_all(hub_v4l2_capture_t *cap, char *err, size_t err_len)
{
    unsigned i;
    for (i = 0; i < cap->nbufs; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[2];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = (uint32_t)cap->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (cap->mplane) {
            buf.m.planes = planes;
            buf.length = (uint32_t)cap->bufs[i].n_planes;
        }
        if (xioctl(cap->fd, VIDIOC_QBUF, &buf) < 0) {
            set_err(err, err_len, "VIDIOC_QBUF failed");
            return 0;
        }
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* 对外 API                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * hub_v4l2_open —— 打开摄像头并完成「协商格式 → 建缓冲 → 开流」
 *
 * 流水线：
 *   open 设备 → QUERYCAP 看能力 → S_FMT 设 NV12/分辨率
 *   → init_mmap + queue_all → STREAMON
 * 成功返回句柄；失败返回 NULL，并尽量写入 err。
 *
 * @param device  如 "/dev/video-camera0"
 * @param width   期望宽（驱动可能改成实际值）
 * @param height  期望高
 * @param err     可选错误信息缓冲
 * @param err_len err 长度
 */
hub_v4l2_capture_t *hub_v4l2_open(const char *device, int width, int height, char *err, size_t err_len)
{
    hub_v4l2_capture_t *cap;
    struct v4l2_capability vcap;
    struct v4l2_format fmt;
    int type;
    const char *yuv_env;

    if (!device || width <= 0 || height <= 0) {
        set_err(err, err_len, "invalid args");
        return NULL;
    }

    cap = (hub_v4l2_capture_t *)calloc(1, sizeof(*cap));
    if (!cap) {
        set_err(err, err_len, "calloc failed");
        return NULL;
    }
    cap->fd = -1;
    cap->width = width;
    cap->height = height;
    yuv_env = getenv("HUB_CAMERA_YUV");
    cap->nv21 = (yuv_env && strcmp(yuv_env, "nv21") == 0) ? 1 : 0;

    /* O_NONBLOCK：无帧时不永久阻塞，配合 select 等待 */
    cap->fd = open(device, O_RDWR | O_NONBLOCK, 0);
    if (cap->fd < 0) {
        set_err(err, err_len, strerror(errno));
        free(cap);
        return NULL;
    }

    memset(&vcap, 0, sizeof(vcap));
    if (xioctl(cap->fd, VIDIOC_QUERYCAP, &vcap) < 0) {
        set_err(err, err_len, "VIDIOC_QUERYCAP failed");
        hub_v4l2_close(cap);
        return NULL;
    }

    {
        int can_mplane = (vcap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
        int can_single = (vcap.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
        if (!can_mplane && !can_single) {
            set_err(err, err_len, "no VIDEO_CAPTURE(_MPLANE)");
            hub_v4l2_close(cap);
            return NULL;
        }
        /* Rockchip CSI 优先走 multiplanar */
        cap->mplane = can_mplane ? 1 : 0;
        cap->buf_type = cap->mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                    : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = (uint32_t)cap->buf_type;
    if (cap->mplane) {
        fmt.fmt.pix_mp.width = (uint32_t)width;
        fmt.fmt.pix_mp.height = (uint32_t)height;
        fmt.fmt.pix_mp.pixelformat = cap->nv21 ? V4L2_PIX_FMT_NV21 : V4L2_PIX_FMT_NV12;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes = 1;
    } else {
        fmt.fmt.pix.width = (uint32_t)width;
        fmt.fmt.pix.height = (uint32_t)height;
        fmt.fmt.pix.pixelformat = cap->nv21 ? V4L2_PIX_FMT_NV21 : V4L2_PIX_FMT_NV12;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }

    if (xioctl(cap->fd, VIDIOC_S_FMT, &fmt) < 0) {
        if (cap->mplane) {
            /* 部分节点需要 2 个 plane 才能设成功 */
            fmt.fmt.pix_mp.num_planes = 2;
            if (xioctl(cap->fd, VIDIOC_S_FMT, &fmt) < 0) {
                set_err(err, err_len, "VIDIOC_S_FMT NV12 failed");
                hub_v4l2_close(cap);
                return NULL;
            }
        } else {
            set_err(err, err_len, "VIDIOC_S_FMT NV12 failed");
            hub_v4l2_close(cap);
            return NULL;
        }
    }

    /* 以驱动真正采纳的参数为准 */
    if (cap->mplane) {
        cap->width = (int)fmt.fmt.pix_mp.width;
        cap->height = (int)fmt.fmt.pix_mp.height;
        cap->stride = (int)fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    } else {
        cap->width = (int)fmt.fmt.pix.width;
        cap->height = (int)fmt.fmt.pix.height;
        cap->stride = (int)fmt.fmt.pix.bytesperline;
    }
    if (cap->stride <= 0) {
        cap->stride = cap->width;
    }

    fprintf(stderr, "[hub_v4l2] %s %dx%d stride=%d mplane=%d fmt=%s\n", device, cap->width,
            cap->height, cap->stride, cap->mplane, cap->nv21 ? "NV21" : "NV12");

    if (!init_mmap(cap, err, err_len) || !queue_all(cap, err, err_len) || !ensure_packed(cap)) {
        if (err && err_len && err[0] == '\0') {
            set_err(err, err_len, "mmap/pack failed");
        }
        hub_v4l2_close(cap);
        return NULL;
    }

    type = cap->buf_type;
    if (xioctl(cap->fd, VIDIOC_STREAMON, &type) < 0) {
        set_err(err, err_len, "VIDIOC_STREAMON failed");
        hub_v4l2_close(cap);
        return NULL;
    }
    return cap;
}

/**
 * hub_v4l2_close —— 停流、解除映射、关闭 fd、释放句柄
 *
 * 顺序：STREAMOFF → munmap → free(packed) → close(fd) → free(cap)
 * 与 open 对称，避免设备占死或内存泄漏。
 */
void hub_v4l2_close(hub_v4l2_capture_t *cap)
{
    unsigned i;
    int p;
    if (!cap) {
        return;
    }
    if (cap->fd >= 0) {
        int type = cap->buf_type;
        xioctl(cap->fd, VIDIOC_STREAMOFF, &type);
    }
    for (i = 0; i < cap->nbufs; ++i) {
        for (p = 0; p < cap->bufs[i].n_planes; ++p) {
            if (cap->bufs[i].start[p] && cap->bufs[i].start[p] != MAP_FAILED) {
                munmap(cap->bufs[i].start[p], cap->bufs[i].length[p]);
            }
            cap->bufs[i].start[p] = NULL;
            cap->bufs[i].length[p] = 0;
        }
        cap->bufs[i].n_planes = 0;
    }
    cap->nbufs = 0;
    free(cap->packed);
    cap->packed = NULL;
    cap->packed_size = 0;
    if (cap->fd >= 0) {
        close(cap->fd);
        cap->fd = -1;
    }
    free(cap);
}

/**
 * hub_v4l2_read_rgb —— 从环形缓冲取出一帧，并转换成 RGB888 写入 rgb_out
 *
 * 流程：
 *   select 等待有帧 → DQBUF 取出 →（必要时 pack）→ nv12_to_rgb → QBUF 归还
 *
 * 注意：调用方必须提供至少 width*height*3 字节的 rgb_out；
 *       用完缓冲后一定要 QBUF，否则驱动空槽耗尽，后续取帧会失败。
 *
 * @return 1 成功得到一帧，0 失败/超时
 */
int hub_v4l2_read_rgb(hub_v4l2_capture_t *cap, uint8_t *rgb_out, char *err, size_t err_len)
{
    fd_set fds;
    struct timeval tv;
    struct v4l2_buffer buf;
    struct v4l2_plane planes[2];
    const struct hub_buf *b;
    int r;
    int ok = 0;

    if (!cap || cap->fd < 0 || !rgb_out) {
        set_err(err, err_len, "invalid capture");
        return 0;
    }

    FD_ZERO(&fds);
    FD_SET(cap->fd, &fds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    r = select(cap->fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) {
        set_err(err, err_len, "select timeout/error");
        return 0;
    }

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type = (uint32_t)cap->buf_type;
    buf.memory = V4L2_MEMORY_MMAP;
    if (cap->mplane) {
        buf.m.planes = planes;
        buf.length = 2;
    }
    if (xioctl(cap->fd, VIDIOC_DQBUF, &buf) < 0) {
        set_err(err, err_len, "VIDIOC_DQBUF failed");
        return 0;
    }

    b = &cap->bufs[buf.index];
    if (cap->mplane && b->n_planes >= 2 && b->start[0] && b->start[1]) {
        /* 双 plane：Y / UV 分开映射 */
        pack_nv12((const uint8_t *)b->start[0], cap->stride, (const uint8_t *)b->start[1],
                  cap->stride, cap->width, cap->height, cap->packed);
        nv12_to_rgb(cap->packed, cap->width, cap->height, cap->nv21, rgb_out);
        ok = 1;
    } else if (b->start[0]) {
        const uint8_t *base = (const uint8_t *)b->start[0];
        if (cap->stride == cap->width) {
            /* 单 plane 且无填充：可直接转色 */
            nv12_to_rgb(base, cap->width, cap->height, cap->nv21, rgb_out);
            ok = 1;
        } else {
            /* 单 plane 但有 stride：Y 后接 UV，需先 pack */
            pack_nv12(base, cap->stride, base + cap->stride * cap->height, cap->stride, cap->width,
                      cap->height, cap->packed);
            nv12_to_rgb(cap->packed, cap->width, cap->height, cap->nv21, rgb_out);
            ok = 1;
        }
    }

    if (xioctl(cap->fd, VIDIOC_QBUF, &buf) < 0) {
        set_err(err, err_len, "VIDIOC_QBUF requeue failed");
        return 0;
    }
    if (!ok) {
        set_err(err, err_len, "empty buffer");
    }
    return ok;
}

/** hub_v4l2_width —— 查询实际帧宽 */
int hub_v4l2_width(const hub_v4l2_capture_t *cap)
{
    return cap ? cap->width : 0;
}

/** hub_v4l2_height —— 查询实际帧高 */
int hub_v4l2_height(const hub_v4l2_capture_t *cap)
{
    return cap ? cap->height : 0;
}

/** hub_v4l2_stride —— 查询驱动 Y 平面行跨度（可能大于 width） */
int hub_v4l2_stride(const hub_v4l2_capture_t *cap)
{
    return cap ? cap->stride : 0;
}
