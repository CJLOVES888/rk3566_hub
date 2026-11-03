/*
 * ============================================================================
 * hub_v4l2_capture.h —— V4L2 MPLANE 采集 NV12/NV21 → RGB888
 * ----------------------------------------------------------------------------
 * 供 CameraWorker 调用。
 * ============================================================================
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hub_v4l2_capture hub_v4l2_capture_t;

/** 打开设备并 STREAMON；成功返回句柄，失败返回 NULL（err 可写原因） */
hub_v4l2_capture_t *hub_v4l2_open(const char *device, int width, int height, char *err, size_t err_len);

void hub_v4l2_close(hub_v4l2_capture_t *cap);

/**
 * 读一帧 RGB888（紧密打包，行跨度 = width*3）。
 * 调用方提供缓冲 rgb_out，容量至少 width*height*3。
 * 成功返回 1，失败返回 0。
 */
int hub_v4l2_read_rgb(hub_v4l2_capture_t *cap, uint8_t *rgb_out, char *err, size_t err_len);

int hub_v4l2_width(const hub_v4l2_capture_t *cap);
int hub_v4l2_height(const hub_v4l2_capture_t *cap);
int hub_v4l2_stride(const hub_v4l2_capture_t *cap);

#ifdef __cplusplus
}
#endif
