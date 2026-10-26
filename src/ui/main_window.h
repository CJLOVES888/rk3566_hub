/*
 * ============================================================================
 * main_window.h —— 主窗口（摄像头 + MQTT + SQLite + NPU 检测）
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

    void onPublishTestClicked();
    void onClockTick();

private:
    void buildUi();
    void initStorage();
    void startCamera();
    void shutdownCamera();
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

    QLabel *m_titleLabel = nullptr;
    QLabel *m_videoLabel = nullptr;
    QLabel *m_overlayLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_mqttLabel = nullptr;
    QLabel *m_npuLabel = nullptr;
    QLabel *m_clockLabel = nullptr;
    QPushButton *m_publishBtn = nullptr;

    QThread m_cameraThread;
    vision::CameraWorker *m_cameraWorker = nullptr;

    QThread m_detectorThread;
    vision::Detector *m_detector = nullptr;
    QVector<vision::DetectBox> m_lastBoxes;

    net::MqttClient *m_mqtt = nullptr;
    QString m_deviceId;
    bool m_screenOn = true;
    double m_lastFps = 0.0;
};

} // namespace ui
