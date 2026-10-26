/*
 * ============================================================================
 * mqtt_client.h —— libmosquitto 封装（巴法云 / 通用 MQTT Broker）
 * ----------------------------------------------------------------------------
 * 设计：
 *   - mosquitto_loop_start() 在库内部线程跑网络循环
 *   - 回调里用 QMetaObject::invokeMethod(..., Qt::QueuedConnection)
 *     把事件切回创建本对象的线程（通常是 UI 线程）
 *   - 断线后由库自动重连（reconnect_delay），再由 onConnect 重新 subscribe
 *
 * 巴法云（官方）：
 *   host=mqtt.bemfa.com|bemfa.com  port=9501
 *   方式一：client_id=用户私钥UID，username/password 可空
 *   方式二：client_id 随意，username=appID，password=secretKey
 * ============================================================================
 */

#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

// 必须在全局命名空间前向声明，否则写在 namespace net 里会被当成 net::mosquitto_message
struct mosquitto;
struct mosquitto_message;

namespace net {

struct MqttConfig {
    QString host = QStringLiteral("bemfa.com");
    int port = 9501;
    QString clientId;          // 巴法云方式一 = 私钥 UID
    QString username;          // 方式一可空；方式二 = appID
    QString password;          // 方式一可空；方式二 = secretKey
    QString cmdTopic;          // 订阅：下发指令
    QString eventTopic;        // 发布：事件告警
    QString dataTopic;         // 发布：高频状态（可选）
    int keepaliveSec = 30;
};

class MqttClient : public QObject {
    Q_OBJECT

public:
    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient() override;

    void setConfig(const MqttConfig &cfg);
    const MqttConfig &config() const { return m_cfg; }

    /** 创建客户端并开始连接（非阻塞，结果走信号） */
    bool start();
    void stop();

    bool isConnected() const { return m_connected; }

public slots:
    /** QoS 默认 1：指令/事件必须尽量送达 */
    bool publish(const QString &topic, const QByteArray &payload, int qos = 1);
    bool publishEvent(const QByteArray &payload, int qos = 1);
    bool publishData(const QByteArray &payload, int qos = 0);

signals:
    void connected();
    void disconnected();
    void messageReceived(const QString &topic, const QByteArray &payload);
    void errorOccurred(const QString &message);

private:
    // mosquitto C 回调（静态）→ 转到成员
    static void onConnectThunk(struct mosquitto *mosq, void *obj, int rc);
    static void onDisconnectThunk(struct mosquitto *mosq, void *obj, int rc);
    static void onMessageThunk(struct mosquitto *mosq, void *obj,
                               const struct ::mosquitto_message *msg);
    static void onLogThunk(struct mosquitto *mosq, void *obj, int level,
                           const char *str);

    void handleConnect(int rc);
    void handleDisconnect(int rc);
    void handleMessage(const QString &topic, const QByteArray &payload);

    MqttConfig m_cfg;
    struct mosquitto *m_mosq = nullptr;
    bool m_connected = false;
    bool m_loopStarted = false;
};

} // namespace net
