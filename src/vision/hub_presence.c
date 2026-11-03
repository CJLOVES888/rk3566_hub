/*
 * ============================================================================
 * hub_presence.c —— 灰度缩图帧差
 * ============================================================================
 */

#include "hub_presence.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

struct hub_presence {
    hub_presence_config_t cfg;
    uint8_t *prev;
    int prev_w;
    int prev_h;
    int occupied;
    int frame_counter;
    int hit_streak;
    double last_score;
    int64_t last_motion_ms;
};

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void hub_presence_config_default(hub_presence_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    cfg->enabled = 1;
    cfg->process_every = 2;
    cfg->preview_w = 160;
    cfg->preview_h = 120;
    cfg->pixel_diff = 25;
    cfg->motion_ratio = 0.025;
    cfg->confirm_hits = 3;
    cfg->idle_ms = 30000;
}

hub_presence_t *hub_presence_create(const hub_presence_config_t *cfg)
{
    hub_presence_t *p = (hub_presence_t *)calloc(1, sizeof(*p));
    if (!p) {
        return NULL;
    }
    if (cfg) {
        p->cfg = *cfg;
    } else {
        hub_presence_config_default(&p->cfg);
    }
    if (p->cfg.process_every < 1) {
        p->cfg.process_every = 1;
    }
    if (p->cfg.confirm_hits < 1) {
        p->cfg.confirm_hits = 1;
    }
    if (p->cfg.idle_ms < 3000) {
        p->cfg.idle_ms = 3000;
    }
    if (p->cfg.preview_w < 8) {
        p->cfg.preview_w = 160;
    }
    if (p->cfg.preview_h < 8) {
        p->cfg.preview_h = 120;
    }
    return p;
}

void hub_presence_destroy(hub_presence_t *p)
{
    if (!p) {
        return;
    }
    free(p->prev);
    free(p);
}

void hub_presence_set_enabled(hub_presence_t *p, int on)
{
    if (!p) {
        return;
    }
    p->cfg.enabled = on ? 1 : 0;
    if (!on) {
        hub_presence_reset(p);
    }
}

int hub_presence_enabled(const hub_presence_t *p)
{
    return p && p->cfg.enabled;
}

int hub_presence_occupied(const hub_presence_t *p)
{
    return p ? p->occupied : 0;
}

void hub_presence_reset(hub_presence_t *p)
{
    if (!p) {
        return;
    }
    free(p->prev);
    p->prev = NULL;
    p->prev_w = p->prev_h = 0;
    p->occupied = 0;
    p->frame_counter = 0;
    p->hit_streak = 0;
    p->last_score = 0.0;
    p->last_motion_ms = 0;
}

/** 最近邻缩放到灰度 preview */
static int make_gray_preview(const uint8_t *rgb, int w, int h, int stride, int pw, int ph,
                             uint8_t *out)
{
    int y, x;
    if (!rgb || !out || w <= 0 || h <= 0 || pw <= 0 || ph <= 0) {
        return 0;
    }
    if (stride < w * 3) {
        stride = w * 3;
    }
    for (y = 0; y < ph; ++y) {
        int sy = y * h / ph;
        const uint8_t *row = rgb + sy * stride;
        for (x = 0; x < pw; ++x) {
            int sx = x * w / pw;
            const uint8_t *px = row + sx * 3;
            /* 近似亮度 */
            out[y * pw + x] = (uint8_t)((px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8);
        }
    }
    return 1;
}

void hub_presence_feed_rgb(hub_presence_t *p, const uint8_t *rgb, int w, int h, int stride,
                           hub_presence_result_t *result)
{
    hub_presence_result_t local;
    uint8_t *gray = NULL;
    int pw, ph, total, changed, y, x;
    int64_t now;
    int moving;

    if (result) {
        memset(result, 0, sizeof(*result));
    } else {
        result = &local;
        memset(result, 0, sizeof(local));
    }

    if (!p || !rgb || w <= 0 || h <= 0) {
        return;
    }
    if (!p->cfg.enabled) {
        snprintf(result->status, sizeof(result->status), "值守: 已关闭");
        result->has_status = 1;
        return;
    }

    ++p->frame_counter;
    if ((p->frame_counter % p->cfg.process_every) != 0) {
        result->occupied = p->occupied;
        result->score = p->last_score;
        return;
    }

    pw = p->cfg.preview_w;
    ph = p->cfg.preview_h;
    gray = (uint8_t *)malloc((size_t)pw * (size_t)ph);
    if (!gray || !make_gray_preview(rgb, w, h, stride, pw, ph, gray)) {
        free(gray);
        return;
    }

    if (!p->prev || p->prev_w != pw || p->prev_h != ph) {
        free(p->prev);
        p->prev = gray;
        p->prev_w = pw;
        p->prev_h = ph;
        p->last_motion_ms = now_ms();
        snprintf(result->status, sizeof(result->status), "值守: 监测中");
        result->has_status = 1;
        result->occupied = p->occupied;
        return; /* gray 所有权交给 prev */
    }

    total = pw * ph;
    changed = 0;
    for (y = 0; y < ph; ++y) {
        const uint8_t *a = gray + y * pw;
        const uint8_t *b = p->prev + y * pw;
        for (x = 0; x < pw; ++x) {
            int d = (int)a[x] - (int)b[x];
            if (d < 0) {
                d = -d;
            }
            if (d >= p->cfg.pixel_diff) {
                ++changed;
            }
        }
    }

    memcpy(p->prev, gray, (size_t)total);
    free(gray);

    p->last_score = total > 0 ? (double)changed / (double)total : 0.0;
    result->score = p->last_score;
    result->occupied = p->occupied;

    now = now_ms();
    moving = (p->last_score >= p->cfg.motion_ratio) ? 1 : 0;

    if (moving) {
        p->hit_streak++;
        p->last_motion_ms = now;
        if (!p->occupied && p->hit_streak >= p->cfg.confirm_hits) {
            p->occupied = 1;
            result->near = 1;
            result->occupied = 1;
            snprintf(result->status, sizeof(result->status), "值守: 有人靠近 (%.1f%%)",
                     p->last_score * 100.0);
            result->has_status = 1;
        } else if (p->occupied) {
            snprintf(result->status, sizeof(result->status), "值守: 占用中 (%.1f%%)",
                     p->last_score * 100.0);
            result->has_status = 1;
        }
    } else {
        p->hit_streak = 0;
        if (p->occupied) {
            int64_t idle = now - p->last_motion_ms;
            if (idle >= p->cfg.idle_ms) {
                p->occupied = 0;
                result->away = 1;
                result->occupied = 0;
                result->idle_sec = idle / 1000.0;
                snprintf(result->status, sizeof(result->status), "值守: 无人超时息屏");
                result->has_status = 1;
            } else {
                int left = (int)((p->cfg.idle_ms - idle) / 1000);
                snprintf(result->status, sizeof(result->status), "值守: 静止中 %ds 后息屏", left);
                result->has_status = 1;
            }
        } else {
            snprintf(result->status, sizeof(result->status), "值守: 监测中 (静)");
            result->has_status = 1;
        }
    }

}
