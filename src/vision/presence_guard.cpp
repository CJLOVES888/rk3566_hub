/*
 * ============================================================================
 * presence_guard.cpp —— 纯 Qt 帧差（不依赖 OpenCV，CPU 占用低）
 * ============================================================================
 */

#include "presence_guard.h"

#include <QDateTime>
#include <QtGlobal>
#include <cmath>

namespace vision {

PresenceGuard::PresenceGuard(PresenceConfig cfg, QObject *parent)
    : QObject(parent)
    , m_cfg(cfg)
{
    if (m_cfg.processEvery < 1) {
        m_cfg.processEvery = 1;
    }
    if (m_cfg.confirmHits < 1) {
        m_cfg.confirmHits = 1;
    }
    if (m_cfg.idleMs < 3000) {
        m_cfg.idleMs = 3000;
    }
}

void PresenceGuard::setEnabled(bool on)
{
    m_cfg.enabled = on;
    if (!on) {
        reset();
        emit statusText(QStringLiteral("值守: 已关闭"));
    } else {
        emit statusText(QStringLiteral("值守: 监测中"));
    }
}

void PresenceGuard::setIdleMs(int ms)
{
    m_cfg.idleMs = qMax(3000, ms);
}

void PresenceGuard::setMotionRatio(double ratio)
{
    m_cfg.motionRatio = qBound(0.005, ratio, 0.5);
}

void PresenceGuard::reset()
{
    m_prev = QImage();
    m_occupied = false;
    m_frameCounter = 0;
    m_hitStreak = 0;
    m_lastScore = 0.0;
    m_lastMotionMs = 0;
}

void PresenceGuard::onFrame(const QImage &frame)
{
    if (!m_cfg.enabled || frame.isNull()) {
        return;
    }

    ++m_frameCounter;
    if ((m_frameCounter % m_cfg.processEvery) != 0) {
        return;
    }

    QImage gray = frame.convertToFormat(QImage::Format_Grayscale8);
    gray = gray.scaled(m_cfg.previewW, m_cfg.previewH,
                       Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (gray.isNull()) {
        return;
    }

    if (m_prev.isNull() || m_prev.size() != gray.size()) {
        m_prev = gray;
        m_lastMotionMs = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    const int w = gray.width();
    const int h = gray.height();
    const int bytesA = gray.bytesPerLine();
    const int bytesB = m_prev.bytesPerLine();
    const uchar *a = gray.constBits();
    const uchar *b = m_prev.constBits();

    int changed = 0;
    const int total = w * h;
    for (int y = 0; y < h; ++y) {
        const uchar *rowA = a + y * bytesA;
        const uchar *rowB = b + y * bytesB;
        for (int x = 0; x < w; ++x) {
            if (std::abs(static_cast<int>(rowA[x]) - static_cast<int>(rowB[x]))
                >= m_cfg.pixelDiff) {
                ++changed;
            }
        }
    }

    m_prev = gray;
    m_lastScore = (total > 0) ? (static_cast<double>(changed) / static_cast<double>(total))
                              : 0.0;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool moving = m_lastScore >= m_cfg.motionRatio;

    if (moving) {
        m_hitStreak++;
        m_lastMotionMs = now;
        if (!m_occupied && m_hitStreak >= m_cfg.confirmHits) {
            m_occupied = true;
            emit occupiedChanged(true, m_lastScore);
            emit presenceNear(m_lastScore);
            emit statusText(QStringLiteral("值守: 有人靠近 (%1%%)")
                                .arg(m_lastScore * 100.0, 0, 'f', 1));
        } else if (m_occupied) {
            emit statusText(QStringLiteral("值守: 占用中 (%1%%)")
                                .arg(m_lastScore * 100.0, 0, 'f', 1));
        }
    } else {
        m_hitStreak = 0;
        if (m_occupied) {
            const qint64 idle = now - m_lastMotionMs;
            if (idle >= m_cfg.idleMs) {
                m_occupied = false;
                const double idleSec = idle / 1000.0;
                emit occupiedChanged(false, m_lastScore);
                emit presenceAway(idleSec);
                emit statusText(QStringLiteral("值守: 无人超时息屏"));
            } else {
                const int left = static_cast<int>((m_cfg.idleMs - idle) / 1000);
                emit statusText(QStringLiteral("值守: 静止中 %1s 后息屏").arg(left));
            }
        } else {
            emit statusText(QStringLiteral("值守: 监测中 (静)"));
        }
    }
}

} // namespace vision
