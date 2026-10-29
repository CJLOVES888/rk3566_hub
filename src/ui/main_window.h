/*
 * ============================================================================
 * main_window.h —— 主窗口（摄像头 + 帧差值守 + MQTT + SQLite + NPU）
 * ============================================================================
 */

#pragma once

#include <QWidget>
#include <QImage>
#include <QThread>
#include <QString>
#include <QVector>

#include "vision/detector.h"

class QLabel;
class QTimer;
class QPushButton;

namespace vision {
class CameraWorker;
class PresenceGuard;
}

namespace net {
class MqttClient;
}

namespace ui {

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onFrame(const QImage &frame);
    void onFps(double fps);
    void onCameraError(const QString &message);

    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttMessage(const QString &topic, const QByteArray &payload);
    void onMqttError(const QString &message);

    void onDetections(const QVector<vision::DetectBox> &boxes);
    void onPersonDetected(float conf);
    void onNpuStatus(const QString &text);
    void onNpuError(const QString &message);

    void onPresenceNear(double score);
    void onPresenceAway(double idleSec);
    void onPresenceStatus(const QString &text);

    void onPublishTestClicked();
    void onClockTick();

private:
    void buildUi();
    void initStorage();
    void startCamera();
    void shutdownCamera();
    void startPresence();
    void startMqtt();
    void shutdownMqtt();
    void startDetector();
    void shutdownDetector();

    QString resolveModelPath() const;
    QImage drawBoxes(const QImage &frame) const;

    void setScreenOn(bool on);
    void handleCommand(const QString &action, const QByteArray &raw);
    void refreshMqttStatusLabel();
    void logAndPublishAck(const QString &action, const QString &cmdId);
    void publishPresenceEvent(const QString &action, double scoreOrIdle);

    QLabel *m_titleLabel = nullptr;
    QLabel *m_videoLabel = nullptr;
    QLabel *m_overlayLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_mqttLabel = nullptr;
    QLabel *m_presenceLabel = nullptr;
    QLabel *m_npuLabel = nullptr;
    QLabel *m_clockLabel = nullptr;
    QPushButton *m_publishBtn = nullptr;

    QThread m_cameraThread;
    vision::CameraWorker *m_cameraWorker = nullptr;
    vision::PresenceGuard *m_presence = nullptr;

    QThread m_detectorThread;
    vision::Detector *m_detector = nullptr;
    QVector<vision::DetectBox> m_lastBoxes;

    net::MqttClient *m_mqtt = nullptr;
    QString m_deviceId;
    bool m_screenOn = true;
    bool m_autoScreenFromPresence = true; // 值守自动亮/灭屏
    double m_lastFps = 0.0;
    qint64 m_lastPresenceEventMs = 0;
    qint64 m_lastPresenceStatusMs = 0;
};

} // namespace ui
