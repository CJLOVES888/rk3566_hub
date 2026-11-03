/*
 * ============================================================================
 * hub_db.h —— SQLite 本地持久化（libsqlite3）
 * ----------------------------------------------------------------------------
 * 表：
 *   event_log  — 摄像头/MQTT/系统事件
 *   config     — 键值配置
 * ============================================================================
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hub_db hub_db_t;

/** 打开或创建数据库；自动建表、写入默认配置。失败返回 NULL */
hub_db_t *hub_db_open(const char *db_path);

void hub_db_close(hub_db_t *db);

int hub_db_is_open(const hub_db_t *db);

int hub_db_log_event(hub_db_t *db, const char *type, const char *detail, const char *source);

/**
 * 读取配置到 out（NUL 结尾）。键不存在时写入 default_value（可为 ""）。
 * 成功返回 1。
 */
int hub_db_get_config(const hub_db_t *db, const char *key, char *out, size_t out_sz,
                      const char *default_value);

int hub_db_set_config(hub_db_t *db, const char *key, const char *value);

/** 删除 ts < cutoff 的事件，返回删除行数；失败返回 -1 */
int hub_db_purge_events_older_than(hub_db_t *db, int64_t cutoff_unix_sec);

#ifdef __cplusplus
}
#endif
