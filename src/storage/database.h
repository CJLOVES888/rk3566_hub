/*
 * ============================================================================
 * database.h —— Qt 封装：内部调用 hub_db（libsqlite3）
 * ----------------------------------------------------------------------------
 * 用法：程序启动时 Database::instance().open(...) 一次；
 *       之后任意模块调用 logEvent / getConfig / setConfig。
 * ============================================================================
 */

#pragma once

#include <QString>

namespace storage {

class Database {
public:
    static Database &instance();

    /** dbPath 例如 data/smart_hub.db；目录不存在会自动创建 */
    bool open(const QString &dbPath);
    void close();
    bool isOpen() const;

    bool logEvent(const QString &type,
                  const QString &detail = QString(),
                  const QString &source = QStringLiteral("system"));

    QString getConfig(const QString &key, const QString &defaultValue = QString()) const;
    bool setConfig(const QString &key, const QString &value);

    /** 删除 ts 早于 cutoffUnixSec 的事件（默认保留 7 天） */
    int purgeEventsOlderThan(qint64 cutoffUnixSec);

private:
    Database() = default;
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    void *m_db = nullptr; /* hub_db_t* */
};

} // namespace storage
