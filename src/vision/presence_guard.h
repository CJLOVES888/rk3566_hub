/*
 * ============================================================================
 * presence_guard.h —— Qt 封装：内部调用 hub_presence
 * ============================================================================
 */

#pragma once

#include <QObject>
#include <QImage>
#include <QString>

namespace vision {

struct PresenceConfig {
    bool enabled = true;
    int processEvery = 2;
    int previewW = 160;
    int previewH = 120;
    int pixelDiff = 25;
    double motionRatio = 0.025;
    int confirmHits = 3;
    int idleMs = 30000;
};

class PresenceGuard : public QObject {
    Q_OBJECT

public:
    explicit PresenceGuard(PresenceConfig cfg, QObject *parent = nullptr);
    ~PresenceGuard() override;

    void setEnabled(bool on);
    bool isEnabled() const;
    bool isOccupied() const;
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
    void *m_c = nullptr; /* hub_presence_t* */
    double m_lastScore = 0.0;
};

} // namespace vision
