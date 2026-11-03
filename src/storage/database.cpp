/*
 * ============================================================================
 * database.cpp —— Qt 封装 → hub_db.c
 * ============================================================================
 */

#include "database.h"

#include "hub_db.h"

#include <QByteArray>
#include <QDebug>

namespace storage {

Database &Database::instance()
{
    static Database inst;
    return inst;
}

bool Database::open(const QString &dbPath)
{
    if (m_db) {
        return true;
    }
    const QByteArray path = dbPath.toUtf8();
    hub_db_t *db = hub_db_open(path.constData());
    if (!db) {
        qWarning() << "打开 SQLite 失败:" << dbPath;
        return false;
    }
    m_db = db;
    qInfo() << "SQLite ready:" << dbPath;
    return true;
}

void Database::close()
{
    if (!m_db) {
        return;
    }
    hub_db_close(static_cast<hub_db_t *>(m_db));
    m_db = nullptr;
}

bool Database::isOpen() const
{
    return hub_db_is_open(static_cast<const hub_db_t *>(m_db));
}

bool Database::logEvent(const QString &type, const QString &detail, const QString &source)
{
    if (!m_db) {
        return false;
    }
    const QByteArray t = type.toUtf8();
    const QByteArray d = detail.toUtf8();
    const QByteArray s = source.toUtf8();
    return hub_db_log_event(static_cast<hub_db_t *>(m_db), t.constData(), d.constData(),
                            s.constData())
           != 0;
}

QString Database::getConfig(const QString &key, const QString &defaultValue) const
{
    char buf[512];
    const QByteArray k = key.toUtf8();
    const QByteArray def = defaultValue.toUtf8();
    hub_db_get_config(static_cast<const hub_db_t *>(m_db), k.constData(), buf, sizeof(buf),
                      def.constData());
    return QString::fromUtf8(buf);
}

bool Database::setConfig(const QString &key, const QString &value)
{
    if (!m_db) {
        return false;
    }
    const QByteArray k = key.toUtf8();
    const QByteArray v = value.toUtf8();
    return hub_db_set_config(static_cast<hub_db_t *>(m_db), k.constData(), v.constData()) != 0;
}

int Database::purgeEventsOlderThan(qint64 cutoffUnixSec)
{
    if (!m_db) {
        return 0;
    }
    const int n = hub_db_purge_events_older_than(static_cast<hub_db_t *>(m_db),
                                                 static_cast<int64_t>(cutoffUnixSec));
    return n < 0 ? 0 : n;
}

} // namespace storage
