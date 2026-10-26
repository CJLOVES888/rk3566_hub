/*
 * ============================================================================
 * rknn_engine.h —— RKNN Runtime C 封装（供 C++ Detector 调用）
 * ----------------------------------------------------------------------------
 * 无 RKNN 时（PC 交叉检查 / 未装 librknnrt）：HUB_HAVE_RKNN=0，init 返回错误码。
 * ============================================================================
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RKNN_ENGINE_MAX_BOXES 64

typedef struct {
    int x1, y1, x2, y2;   /* 原图坐标 */
    float conf;
    int class_id;         /* COCO: 0=person */
} rknn_detect_box_t;

typedef struct rknn_engine rknn_engine_t;

/** 创建并加载 .rknn；失败返回 NULL */
rknn_engine_t *rknn_engine_create(const char *model_path);

void rknn_engine_destroy(rknn_engine_t *eng);

/**
 * 对一帧 RGB888 原图做 letterbox → NPU 推理 → YOLOv8 后处理。
 * @param rgb        行优先 RGB，长度 src_w*src_h*3
 * @param conf_thres 置信度阈值
 * @param nms_thres  IoU 阈值
 * @param out        输出框数组
 * @param max_out    out 容量
 * @return           检出框数量；负数为错误
 */
int rknn_engine_detect(rknn_engine_t *eng,
                       const uint8_t *rgb, int src_w, int src_h,
                       float conf_thres, float nms_thres,
                       rknn_detect_box_t *out, int max_out);

/** 最近一次 rknn_run 耗时（微秒），失败为 -1 */
int64_t rknn_engine_last_infer_us(const rknn_engine_t *eng);

const char *rknn_engine_last_error(void);

#ifdef __cplusplus
}
#endif
