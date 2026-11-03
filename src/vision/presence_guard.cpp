/*
 * ============================================================================
 * presence_guard.cpp —— Qt 信号槽 ↔ hub_presence
 * ============================================================================
 */

#include "presence_guard.h"
#include "hub_presence.h"

#include <QtGlobal>
#include <cstring>

namespace vision {

static hub_presence_config_t toC(const PresenceConfig &cfg)
{
    hub_presence_config_t c;
    hub_presence_config_default(&c);
    c.enabled = cfg.enabled ? 1 : 0;
    c.process_every = cfg.processEvery;
    c.preview_w = cfg.previewW;
    c.preview_h = cfg.previewH;
    c.pixel_diff = cfg.pixelDiff;
    c.motion_ratio = cfg.motionRatio;
    c.confirm_hits = cfg.confirmHits;
    c.idle_ms = cfg.idleMs;
    return c;
}

PresenceGuard::PresenceGuard(PresenceConfig cfg, QObject *parent)
    : QObject(parent)
    , m_cfg(cfg)
{
    const hub_presence_config_t c = toC(m_cfg);
    m_c = hub_presence_create(&c);
}

PresenceGuard::~PresenceGuard()
{
    hub_presence_destroy(static_cast<hub_presence_t *>(m_c));
    m_c = nullptr;
}

void PresenceGuard::setEnabled(bool on)
{
    m_cfg.enabled = on;
    hub_presence_set_enabled(static_cast<hub_presence_t *>(m_c), on ? 1 : 0);
    emit statusText(on ? QStringLiteral("值守: 监测中") : QStringLiteral("值守: 已关闭"));
}

bool PresenceGuard::isEnabled() const
{
    return hub_presence_enabled(static_cast<const hub_presence_t *>(m_c)) != 0;
}

bool PresenceGuard::isOccupied() const
{
    return hub_presence_occupied(static_cast<const hub_presence_t *>(m_c)) != 0;
}

void PresenceGuard::setIdleMs(int ms)
{
    m_cfg.idleMs = qMax(3000, ms);
    hub_presence_destroy(static_cast<hub_presence_t *>(m_c));
    const hub_presence_config_t c = toC(m_cfg);
    m_c = hub_presence_create(&c);
}

void PresenceGuard::setMotionRatio(double ratio)
{
    m_cfg.motionRatio = qBound(0.005, ratio, 0.5);
    hub_presence_destroy(static_cast<hub_presence_t *>(m_c));
    const hub_presence_config_t c = toC(m_cfg);
    m_c = hub_presence_create(&c);
}

void PresenceGuard::reset()
{
    hub_presence_reset(static_cast<hub_presence_t *>(m_c));
    m_lastScore = 0.0;
}

void PresenceGuard::onFrame(const QImage &frame)
{
    if (frame.isNull() || !m_c) {
        return;
    }

    QImage rgb = frame;
    if (rgb.format() != QImage::Format_RGB888) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }
    if (rgb.isNull()) {
        return;
    }

    hub_presence_result_t r;
    std::memset(&r, 0, sizeof(r));
    hub_presence_feed_rgb(static_cast<hub_presence_t *>(m_c), rgb.constBits(), rgb.width(),
                          rgb.height(), static_cast<int>(rgb.bytesPerLine()), &r);

    m_lastScore = r.score;
    if (r.near) {
        emit occupiedChanged(true, r.score);
        emit presenceNear(r.score);
    }
    if (r.away) {
        emit occupiedChanged(false, r.score);
        emit presenceAway(r.idle_sec);
    }
    if (r.has_status && r.status[0] != '\0') {
        emit statusText(QString::fromUtf8(r.status));
    }
}

} // namespace vision
