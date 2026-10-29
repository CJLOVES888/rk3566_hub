/*
 * ============================================================================
 * database.cpp —— SQLite 实现（WAL + prepare/bind）
 * ============================================================================
 */

#include "database.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>
#include <QVariant>

namespace storage {

namespace {
constexpr const char *kConnectionName = "rk3566_hub";
}

Database &Database::instance()
{
    static Database inst;
    return inst;
}

bool Database::open(const QString &dbPath)
{
    if (m_open) {
        return true;
    }

    const QFileInfo fi(dbPath);
    if (!QDir().mkpath(fi.absolutePath())) {
        qWarning() << "无法创建数据库目录:" << fi.absolutePath();
        return false;
    }

    if (QSqlDatabase::contains(kConnectionName)) {
        QSqlDatabase::removeDatabase(kConnectionName);
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "打开 SQLite 失败:" << db.lastError().text();
        return false;
    }

    {
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA journal_mode=WAL;"));
        q.exec(QStringLiteral("PRAGMA synchronous=NORMAL;"));
    }

    if (!ensureSchema() || !seedDefaults()) {
        db.close();
        return false;
    }

    m_open = true;
    qInfo() << "SQLite ready:" << dbPath;
    return true;
}

void Database::close()
{
    if (!m_open) {
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(kConnectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(kConnectionName);
    m_open = false;
}

bool Database::isOpen() const
{
    return m_open;
}

bool Database::ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    QSqlQuery q(db);

    const bool okLog = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS event_log ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts INTEGER NOT NULL,"
        "  type TEXT NOT NULL,"
        "  detail TEXT,"
        "  source TEXT"
        ");"));
    if (!okLog) {
        qWarning() << "创建 event_log 失败:" << q.lastError().text();
        return false;
    }

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_event_log_ts ON event_log(ts);"));

    const bool okCfg = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS config ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ");"));
    if (!okCfg) {
        qWarning() << "创建 config 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

bool Database::seedDefaults()
{
    // 仅在键不存在时写入默认值，避免覆盖用户已改配置
    const struct {
        const char *key;
        const char *value;
    } defaults[] = {
        {"device_id", "dev01"},
        {"mqtt_host", "mqtt.bemfa.com"},
        {"mqtt_port", "9501"},
        {"mqtt_cmd_topic", "rk3566cmd"},
        {"mqtt_event_topic", "rk3566event"},
        {"npu_conf_threshold", "0.6"},
        {"presence_enable", "1"},
        {"presence_idle_sec", "30"},
        {"presence_thresh", "0.025"},
        {"presence_confirm", "3"},
    };

    for (const auto &d : defaults) {
        if (getConfig(QString::fromUtf8(d.key)).isEmpty()) {
            if (!setConfig(QString::fromUtf8(d.key), QString::fromUtf8(d.value))) {
                return false;
            }
        }
    }

    // 旧版默认值迁移（巴法云 MQTT 设备云）
    if (getConfig(QStringLiteral("mqtt_host")) == QLatin1String("bemfa.com")) {
        setConfig(QStringLiteral("mqtt_host"), QStringLiteral("mqtt.bemfa.com"));
    }
    if (getConfig(QStringLiteral("mqtt_cmd_topic")).isEmpty()) {
        setConfig(QStringLiteral("mqtt_cmd_topic"), QStringLiteral("rk3566cmd"));
    }
    if (getConfig(QStringLiteral("mqtt_event_topic")).isEmpty()) {
        setConfig(QStringLiteral("mqtt_event_topic"), QStringLiteral("rk3566event"));
    }
    return true;
}

bool Database::logEvent(const QString &type, const QString &detail, const QString &source)
{
    if (!m_open) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO event_log(ts, type, detail, source) VALUES(?, ?, ?, ?)"));
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(type);
    q.addBindValue(detail);
    q.addBindValue(source);
    if (!q.exec()) {
        qWarning() << "logEvent 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

QString Database::getConfig(const QString &key, const QString &defaultValue) const
{
    if (!m_open && !QSqlDatabase::contains(kConnectionName)) {
        return defaultValue;
    }
    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    if (!db.isOpen()) {
        return defaultValue;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT value FROM config WHERE key=?"));
    q.addBindValue(key);
    if (!q.exec() || !q.next()) {
        return defaultValue;
    }
    return q.value(0).toString();
}

bool Database::setConfig(const QString &key, const QString &value)
{
    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO config(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    if (!q.exec()) {
        qWarning() << "setConfig 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

int Database::purgeEventsOlderThan(qint64 cutoffUnixSec)
{
    if (!m_open) {
        return 0;
    }
    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM event_log WHERE ts < ?"));
    q.addBindValue(cutoffUnixSec);
    if (!q.exec()) {
        qWarning() << "purgeEvents 失败:" << q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

} // namespace storage
