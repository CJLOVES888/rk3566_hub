/*
 * ============================================================================
 * hub_presence.h —— RGB 帧差占用检测
 * ============================================================================
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hub_presence_config {
    int enabled;
    int process_every;   /* 默认 2 */
    int preview_w;       /* 默认 160 */
    int preview_h;       /* 默认 120 */
    int pixel_diff;      /* 默认 25 */
    double motion_ratio; /* 默认 0.025 */
    int confirm_hits;    /* 默认 3 */
    int idle_ms;         /* 默认 30000 */
} hub_presence_config_t;

typedef struct hub_presence hub_presence_t;

typedef struct hub_presence_result {
    int near;          /* 1 = 本帧触发有人靠近 */
    int away;          /* 1 = 本帧触发无人超时 */
    int occupied;      /* 当前占用状态 */
    double score;      /* 最近运动占比 */
    double idle_sec;   /* away 时有效 */
    char status[96];   /* 状态文案 */
    int has_status;    /* status 是否更新 */
} hub_presence_result_t;

void hub_presence_config_default(hub_presence_config_t *cfg);

hub_presence_t *hub_presence_create(const hub_presence_config_t *cfg);
void hub_presence_destroy(hub_presence_t *p);

void hub_presence_set_enabled(hub_presence_t *p, int on);
int hub_presence_enabled(const hub_presence_t *p);
int hub_presence_occupied(const hub_presence_t *p);
void hub_presence_reset(hub_presence_t *p);

/**
 * 喂入一帧 RGB888（stride 为每行字节数，通常 w*3）。
 * result 输出本帧事件；可传 NULL。
 */
void hub_presence_feed_rgb(hub_presence_t *p, const uint8_t *rgb, int w, int h, int stride,
                           hub_presence_result_t *result);

#ifdef __cplusplus
}
#endif
