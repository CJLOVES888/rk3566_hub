/*
 * ============================================================================
 * mqtt_client.h —— Qt 封装：内部调用 hub_mqtt
 * ----------------------------------------------------------------------------
 * MainWindow 仍用本类的信号槽；mosquitto 逻辑在 hub_mqtt.c。
 * ============================================================================
 */

#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

namespace net {

struct MqttConfig {
    QString host = QStringLiteral("mqtt.bemfa.com");
    int port = 9501;
    QString clientId;
    QString username;
    QString password;
    QString cmdTopic;
    QString eventTopic;
    QString dataTopic;
    int keepaliveSec = 30;
};

class MqttClient : public QObject {
    Q_OBJECT

public:
    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient() override;

    void setConfig(const MqttConfig &cfg);
    const MqttConfig &config() const { return m_cfg; }

    bool start();
    void stop();
    bool isConnected() const;

public slots:
    bool publish(const QString &topic, const QByteArray &payload, int qos = 1);
    bool publishEvent(const QByteArray &payload, int qos = 1);
    bool publishData(const QByteArray &payload, int qos = 0);

signals:
    void connected();
    void disconnected();
    void messageReceived(const QString &topic, const QByteArray &payload);
    void errorOccurred(const QString &message);

private:
    static void onConnectedC(void *user);
    static void onDisconnectedC(void *user, int rc);
    static void onMessageC(void *user, const char *topic, const void *payload, int len);
    static void onErrorC(void *user, const char *message);

    MqttConfig m_cfg;
    void *m_c = nullptr; /* hub_mqtt_t* */
};

} // namespace net
