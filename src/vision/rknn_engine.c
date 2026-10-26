/*
 * ============================================================================
 * rknn_engine.c —— RKNN + YOLOv8n 后处理（float 输出简化版）
 * ----------------------------------------------------------------------------
 * 假设 ultralytics YOLOv8n 转 RKNN 后主输出接近 [1, 84, 8400]：
 *   84 = 4(box) + 80(COCO classes)；8400 = 锚点数。
 * 使用 rknn_outputs_get(..., want_float=1) 便于第一版跑通。
 * ============================================================================
 */

#include "rknn_engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#if defined(HUB_HAVE_RKNN) && HUB_HAVE_RKNN
#include "rknn_api.h"
#endif

#define MODEL_SIZE 640
#define YOLO_CLASSES 80

static char g_last_error[256] = {0};

void rknn_engine_set_error(const char *msg)
{
    snprintf(g_last_error, sizeof(g_last_error), "%s", msg ? msg : "");
}

const char *rknn_engine_last_error(void)
{
    return g_last_error;
}

#if !(defined(HUB_HAVE_RKNN) && HUB_HAVE_RKNN)

struct rknn_engine {
    int dummy;
};

rknn_engine_t *rknn_engine_create(const char *model_path)
{
    (void)model_path;
    rknn_engine_set_error("编译时未启用 HUB_HAVE_RKNN（板子需链接 librknnrt）");
    return NULL;
}

void rknn_engine_destroy(rknn_engine_t *eng)
{
    free(eng);
}

int rknn_engine_detect(rknn_engine_t *eng, const uint8_t *rgb, int src_w, int src_h,
                       float conf_thres, float nms_thres,
                       rknn_detect_box_t *out, int max_out)
{
    (void)eng; (void)rgb; (void)src_w; (void)src_h;
    (void)conf_thres; (void)nms_thres; (void)out; (void)max_out;
    return -1;
}

int64_t rknn_engine_last_infer_us(const rknn_engine_t *eng)
{
    (void)eng;
    return -1;
}

#else /* HUB_HAVE_RKNN */

struct rknn_engine {
    rknn_context ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs;
    rknn_tensor_attr *output_attrs;
    int model_w;
    int model_h;
    int64_t last_infer_us;
    uint8_t *letterbox_buf; /* MODEL_SIZE * MODEL_SIZE * 3 */
};

static int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static unsigned char *load_model_file(const char *path, int *size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        rknn_engine_set_error("无法打开模型文件");
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    int sz = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc((size_t)sz);
    if (!data) {
        fclose(fp);
        rknn_engine_set_error("malloc 模型缓冲失败");
        return NULL;
    }
    if ((int)fread(data, 1, (size_t)sz, fp) != sz) {
        free(data);
        fclose(fp);
        rknn_engine_set_error("读取模型失败");
        return NULL;
    }
    fclose(fp);
    *size = sz;
    return data;
}

/* letterbox：保持比例缩放到 model，其余填 114 */
static void letterbox_rgb(const uint8_t *src, int sw, int sh,
                          uint8_t *dst, int dw, int dh,
                          float *out_scale, int *out_pad_x, int *out_pad_y)
{
    const float scale = fminf((float)dw / (float)sw, (float)dh / (float)sh);
    const int nw = (int)(sw * scale);
    const int nh = (int)(sh * scale);
    const int pad_x = (dw - nw) / 2;
    const int pad_y = (dh - nh) / 2;

    memset(dst, 114, (size_t)dw * (size_t)dh * 3);

    for (int y = 0; y < nh; ++y) {
        const int sy = (int)(y / scale);
        for (int x = 0; x < nw; ++x) {
            const int sx = (int)(x / scale);
            const uint8_t *p = src + ((size_t)sy * (size_t)sw + (size_t)sx) * 3;
            uint8_t *q = dst + ((size_t)(y + pad_y) * (size_t)dw + (size_t)(x + pad_x)) * 3;
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
        }
    }
    *out_scale = scale;
    *out_pad_x = pad_x;
    *out_pad_y = pad_y;
}

static float iou_box(const rknn_detect_box_t *a, const rknn_detect_box_t *b)
{
    const int xx1 = a->x1 > b->x1 ? a->x1 : b->x1;
    const int yy1 = a->y1 > b->y1 ? a->y1 : b->y1;
    const int xx2 = a->x2 < b->x2 ? a->x2 : b->x2;
    const int yy2 = a->y2 < b->y2 ? a->y2 : b->y2;
    const int w = xx2 - xx1;
    const int h = yy2 - yy1;
    if (w <= 0 || h <= 0) {
        return 0.f;
    }
    const float inter = (float)w * (float)h;
    const float area_a = (float)(a->x2 - a->x1) * (float)(a->y2 - a->y1);
    const float area_b = (float)(b->x2 - b->x1) * (float)(b->y2 - b->y1);
    return inter / (area_a + area_b - inter + 1e-6f);
}

static int nms_inplace(rknn_detect_box_t *boxes, int n, float nms_thres)
{
    /* 简单按 conf 冒泡排序 */
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (boxes[j].conf > boxes[i].conf) {
                rknn_detect_box_t t = boxes[i];
                boxes[i] = boxes[j];
                boxes[j] = t;
            }
        }
    }
    char suppressed[RKNN_ENGINE_MAX_BOXES];
    memset(suppressed, 0, sizeof(suppressed));
    int kept = 0;
    rknn_detect_box_t tmp[RKNN_ENGINE_MAX_BOXES];
    for (int i = 0; i < n; ++i) {
        if (suppressed[i]) {
            continue;
        }
        tmp[kept++] = boxes[i];
        for (int j = i + 1; j < n; ++j) {
            if (suppressed[j]) {
                continue;
            }
            if (boxes[i].class_id == boxes[j].class_id &&
                iou_box(&boxes[i], &boxes[j]) > nms_thres) {
                suppressed[j] = 1;
            }
        }
    }
    memcpy(boxes, tmp, (size_t)kept * sizeof(rknn_detect_box_t));
    return kept;
}

/*
 * YOLOv8 导出常见布局：输出 float[1, 84, N] 或 [1, N, 84]
 * 我们根据 dims 自动判断。
 */
static int decode_yolov8(const float *data, const int *dims, int n_dims,
                         int src_w, int src_h, float scale, int pad_x, int pad_y,
                         float conf_thres, rknn_detect_box_t *out, int max_out)
{
    int channels = 0;
    int anchors = 0;
    int layout_cn = 0; /* 1: [1,C,N]  2: [1,N,C] */

    if (n_dims >= 3) {
        /* dims 顺序依 RKNN 查询结果，常见 [1,84,8400] 或 [1,8400,84] */
        if (dims[1] == 84 || dims[1] == 4 + YOLO_CLASSES) {
            channels = dims[1];
            anchors = dims[2];
            layout_cn = 1;
        } else if (dims[2] == 84 || dims[2] == 4 + YOLO_CLASSES) {
            anchors = dims[1];
            channels = dims[2];
            layout_cn = 2;
        } else if (dims[1] > dims[2]) {
            channels = dims[1];
            anchors = dims[2];
            layout_cn = 1;
        } else {
            anchors = dims[1];
            channels = dims[2];
            layout_cn = 2;
        }
    } else {
        rknn_engine_set_error("YOLO 输出维度无法识别");
        return -1;
    }

    const int num_classes = channels - 4;
    if (num_classes <= 0) {
        rknn_engine_set_error("YOLO 通道数异常");
        return -1;
    }

    int count = 0;
    for (int i = 0; i < anchors && count < max_out; ++i) {
        float box[4];
        float best = 0.f;
        int best_id = 0;

        if (layout_cn == 1) {
            /* data[c * anchors + i] */
            for (int k = 0; k < 4; ++k) {
                box[k] = data[k * anchors + i];
            }
            for (int c = 0; c < num_classes; ++c) {
                const float s = data[(4 + c) * anchors + i];
                if (s > best) {
                    best = s;
                    best_id = c;
                }
            }
        } else {
            /* data[i * channels + c] */
            const float *row = data + (size_t)i * (size_t)channels;
            for (int k = 0; k < 4; ++k) {
                box[k] = row[k];
            }
            for (int c = 0; c < num_classes; ++c) {
                const float s = row[4 + c];
                if (s > best) {
                    best = s;
                    best_id = c;
                }
            }
        }

        if (best < conf_thres) {
            continue;
        }

        /* box = cx,cy,w,h （模型输入坐标） */
        float cx = box[0], cy = box[1], w = box[2], h = box[3];
        float x1 = (cx - w * 0.5f - (float)pad_x) / scale;
        float y1 = (cy - h * 0.5f - (float)pad_y) / scale;
        float x2 = (cx + w * 0.5f - (float)pad_x) / scale;
        float y2 = (cy + h * 0.5f - (float)pad_y) / scale;

        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > src_w - 1) x2 = (float)(src_w - 1);
        if (y2 > src_h - 1) y2 = (float)(src_h - 1);
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        out[count].x1 = (int)x1;
        out[count].y1 = (int)y1;
        out[count].x2 = (int)x2;
        out[count].y2 = (int)y2;
        out[count].conf = best;
        out[count].class_id = best_id;
        ++count;
    }
    return count;
}

rknn_engine_t *rknn_engine_create(const char *model_path)
{
    rknn_engine_set_error("");
    if (!model_path || !model_path[0]) {
        rknn_engine_set_error("模型路径为空");
        return NULL;
    }

    int model_size = 0;
    unsigned char *model_data = load_model_file(model_path, &model_size);
    if (!model_data) {
        return NULL;
    }

    rknn_engine_t *eng = (rknn_engine_t *)calloc(1, sizeof(rknn_engine_t));
    if (!eng) {
        free(model_data);
        rknn_engine_set_error("calloc engine 失败");
        return NULL;
    }

    int ret = rknn_init(&eng->ctx, model_data, model_size, 0, NULL);
    free(model_data);
    if (ret != RKNN_SUCC) {
        snprintf(g_last_error, sizeof(g_last_error), "rknn_init 失败 ret=%d", ret);
        free(eng);
        return NULL;
    }

    ret = rknn_query(eng->ctx, RKNN_QUERY_IN_OUT_NUM, &eng->io_num, sizeof(eng->io_num));
    if (ret != RKNN_SUCC || eng->io_num.n_input < 1 || eng->io_num.n_output < 1) {
        rknn_engine_set_error("rknn_query IN_OUT_NUM 失败");
        rknn_destroy(eng->ctx);
        free(eng);
        return NULL;
    }

    eng->input_attrs = (rknn_tensor_attr *)calloc(eng->io_num.n_input, sizeof(rknn_tensor_attr));
    eng->output_attrs = (rknn_tensor_attr *)calloc(eng->io_num.n_output, sizeof(rknn_tensor_attr));
    if (!eng->input_attrs || !eng->output_attrs) {
        rknn_engine_set_error("calloc attrs 失败");
        rknn_engine_destroy(eng);
        return NULL;
    }

    for (uint32_t i = 0; i < eng->io_num.n_input; ++i) {
        eng->input_attrs[i].index = i;
        rknn_query(eng->ctx, RKNN_QUERY_INPUT_ATTR, &eng->input_attrs[i], sizeof(rknn_tensor_attr));
    }
    for (uint32_t i = 0; i < eng->io_num.n_output; ++i) {
        eng->output_attrs[i].index = i;
        rknn_query(eng->ctx, RKNN_QUERY_OUTPUT_ATTR, &eng->output_attrs[i], sizeof(rknn_tensor_attr));
    }

    /* 输入尺寸：NHWC [1,H,W,C] 或 NCHW [1,C,H,W] */
    if (eng->input_attrs[0].fmt == RKNN_TENSOR_NHWC) {
        eng->model_h = (int)eng->input_attrs[0].dims[1];
        eng->model_w = (int)eng->input_attrs[0].dims[2];
    } else {
        eng->model_h = (int)eng->input_attrs[0].dims[2];
        eng->model_w = (int)eng->input_attrs[0].dims[3];
    }
    if (eng->model_w <= 0 || eng->model_h <= 0) {
        eng->model_w = MODEL_SIZE;
        eng->model_h = MODEL_SIZE;
    }

    eng->letterbox_buf = (uint8_t *)malloc((size_t)eng->model_w * (size_t)eng->model_h * 3);
    if (!eng->letterbox_buf) {
        rknn_engine_set_error("malloc letterbox 失败");
        rknn_engine_destroy(eng);
        return NULL;
    }

    printf("[rknn] model input %dx%d, outputs=%u\n",
           eng->model_w, eng->model_h, eng->io_num.n_output);
    return eng;
}

void rknn_engine_destroy(rknn_engine_t *eng)
{
    if (!eng) {
        return;
    }
    if (eng->ctx) {
        rknn_destroy(eng->ctx);
        eng->ctx = 0;
    }
    free(eng->input_attrs);
    free(eng->output_attrs);
    free(eng->letterbox_buf);
    free(eng);
}

int rknn_engine_detect(rknn_engine_t *eng, const uint8_t *rgb, int src_w, int src_h,
                       float conf_thres, float nms_thres,
                       rknn_detect_box_t *out, int max_out)
{
    if (!eng || !rgb || !out || max_out <= 0 || src_w <= 0 || src_h <= 0) {
        return -1;
    }
    if (max_out > RKNN_ENGINE_MAX_BOXES) {
        max_out = RKNN_ENGINE_MAX_BOXES;
    }

    float scale = 1.f;
    int pad_x = 0, pad_y = 0;
    letterbox_rgb(rgb, src_w, src_h, eng->letterbox_buf,
                  eng->model_w, eng->model_h, &scale, &pad_x, &pad_y);

    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    input.size = (uint32_t)eng->model_w * (uint32_t)eng->model_h * 3;
    input.buf = eng->letterbox_buf;

    int ret = rknn_inputs_set(eng->ctx, 1, &input);
    if (ret != RKNN_SUCC) {
        snprintf(g_last_error, sizeof(g_last_error), "rknn_inputs_set 失败 %d", ret);
        return -1;
    }

    const int64_t t0 = now_us();
    ret = rknn_run(eng->ctx, NULL);
    const int64_t t1 = now_us();
    eng->last_infer_us = t1 - t0;
    if (ret != RKNN_SUCC) {
        snprintf(g_last_error, sizeof(g_last_error), "rknn_run 失败 %d", ret);
        return -1;
    }

    /* 取第 0 路输出为检测头（多数单输出 YOLOv8） */
    rknn_output output;
    memset(&output, 0, sizeof(output));
    output.want_float = 1;
    output.is_prealloc = 0;

    ret = rknn_outputs_get(eng->ctx, 1, &output, NULL);
    if (ret != RKNN_SUCC) {
        snprintf(g_last_error, sizeof(g_last_error), "rknn_outputs_get 失败 %d", ret);
        return -1;
    }

    int dims_i[16];
    memset(dims_i, 0, sizeof(dims_i));
    const int n_dims = (int)eng->output_attrs[0].n_dims;
    for (int i = 0; i < n_dims && i < 16; ++i) {
        dims_i[i] = (int)eng->output_attrs[0].dims[i];
    }

    rknn_detect_box_t raw[RKNN_ENGINE_MAX_BOXES];
    int n = decode_yolov8((const float *)output.buf, dims_i, n_dims,
                          src_w, src_h, scale, pad_x, pad_y,
                          conf_thres, raw, RKNN_ENGINE_MAX_BOXES);
    rknn_outputs_release(eng->ctx, 1, &output);

    if (n < 0) {
        return -1;
    }
    n = nms_inplace(raw, n, nms_thres);
    if (n > max_out) {
        n = max_out;
    }
    memcpy(out, raw, (size_t)n * sizeof(rknn_detect_box_t));
    return n;
}

int64_t rknn_engine_last_infer_us(const rknn_engine_t *eng)
{
    return eng ? eng->last_infer_us : -1;
}

#endif /* HUB_HAVE_RKNN */
