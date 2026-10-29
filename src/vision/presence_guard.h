/*
 * ============================================================================
 * presence_guard.h —— 帧差占用检测（方案 A，无需 NPU）
 * ----------------------------------------------------------------------------
 * 对缩小后的灰度图做帧差，估计「画面有人活动」：
 *   - 连续多帧超阈值 → presenceNear（亮屏 / MQTT）
 *   - 超过 idle 秒无显著运动 → presenceAway（息屏）
 * ============================================================================
 */

#pragma once

#include <QObject>
#include <QImage>
#include <QString>

namespace vision {

struct PresenceConfig {
    bool enabled = true;
    int processEvery = 2;       // 每 N 帧算 1 次
    int previewW = 160;
    int previewH = 120;
    int pixelDiff = 25;         // 单像素差阈值
    double motionRatio = 0.025; // 变化像素占比
    int confirmHits = 3;        // 连续命中才判「有人」
    int idleMs = 30000;         // 无人多久息屏
};

class PresenceGuard : public QObject {
    Q_OBJECT

public:
    explicit PresenceGuard(PresenceConfig cfg, QObject *parent = nullptr);

    void setEnabled(bool on);
    bool isEnabled() const { return m_cfg.enabled; }
    bool isOccupied() const { return m_occupied; }
    double lastScore() const { return m_lastScore; }
    void setIdleMs(int ms);
    void setMotionRatio(double ratio);

public slots:
    void onFrame(const QImage &frame);
    void reset();

signals:
    void occupiedChanged(bool occupied, double score);
    void presenceNear(double score);
    void presenceAway(double idleSec);
    void statusText(const QString &text);

private:
    PresenceConfig m_cfg;
    QImage m_prev;
    bool m_occupied = false;
    int m_frameCounter = 0;
    int m_hitStreak = 0;
    double m_lastScore = 0.0;
    qint64 m_lastMotionMs = 0;
};

} // namespace vision
