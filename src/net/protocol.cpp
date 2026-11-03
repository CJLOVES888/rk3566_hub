/*
 * ============================================================================
 * protocol.cpp —— Qt 封装 → hub_protocol.c
 * ============================================================================
 */

#include "protocol.h"

#include "hub_protocol.h"

#include <QJsonDocument>
#include <cstdlib>

namespace net {

QString makeMsgId()
{
    char id[16];
    hub_protocol_make_id(id, sizeof(id));
    return QString::fromUtf8(id);
}

qint64 nowTs()
{
    return static_cast<qint64>(hub_protocol_now_ts());
}

QByteArray buildMessage(const QString &type,
                        const QJsonObject &payload,
                        const QString &id)
{
    const QByteArray payloadJson =
        QJsonDocument(payload).toJson(QJsonDocument::Compact);
    cJSON *payloadObj = cJSON_Parse(payloadJson.constData());
    if (!payloadObj) {
        payloadObj = cJSON_CreateObject();
    }

    const QByteArray typeUtf8 = type.toUtf8();
    const QByteArray idUtf8 = id.toUtf8();
    char *out = nullptr;
    if (!hub_protocol_build(typeUtf8.constData(), payloadObj,
                            id.isEmpty() ? nullptr : idUtf8.constData(), &out)
        || !out) {
        return {};
    }
    const QByteArray result(out);
    free(out);
    return result;
}

bool parseMessage(const QByteArray &raw, QJsonObject *out)
{
    if (!out) {
        return false;
    }
    cJSON *root = nullptr;
    if (!hub_protocol_parse(raw.constData(), static_cast<size_t>(raw.size()), &root)
        || !root) {
        return false;
    }
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(printed));
    free(printed);
    if (!doc.isObject()) {
        return false;
    }
    *out = doc.object();
    return true;
}

QString payloadAction(const QJsonObject &root)
{
    const QByteArray raw = QJsonDocument(root).toJson(QJsonDocument::Compact);
    cJSON *obj = cJSON_Parse(raw.constData());
    if (!obj) {
        return {};
    }
    char action[128];
    const int ok = hub_protocol_payload_action(obj, action, sizeof(action));
    cJSON_Delete(obj);
    return ok ? QString::fromUtf8(action) : QString();
}

} // namespace net
