/*
 * ============================================================================
 * hub_db.c —— SQLite WAL + prepare/bind
 * ============================================================================
 */

#include "hub_db.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <sqlite3.h>

struct hub_db {
    sqlite3 *db;
};

static int mkdir_p(const char *dir)
{
    char tmp[512];
    size_t len;
    char *p;

    if (!dir || !dir[0]) {
        return -1;
    }
    len = strlen(dir);
    if (len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, dir, len + 1);

    for (p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int ensure_parent_dir(const char *db_path)
{
    char dir[512];
    const char *slash;
    size_t n;

    if (!db_path) {
        return -1;
    }
    slash = strrchr(db_path, '/');
#if defined(_WIN32)
    {
        const char *b = strrchr(db_path, '\\');
        if (b && (!slash || b > slash)) {
            slash = b;
        }
    }
#endif
    if (!slash) {
        return 0;
    }
    n = (size_t)(slash - db_path);
    if (n == 0 || n >= sizeof(dir)) {
        return -1;
    }
    memcpy(dir, db_path, n);
    dir[n] = '\0';
    return mkdir_p(dir);
}

static int exec_simple(sqlite3 *db, const char *sql)
{
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "hub_db SQL error: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return 0;
    }
    return 1;
}

static int ensure_schema(sqlite3 *db)
{
    if (!exec_simple(db,
                     "CREATE TABLE IF NOT EXISTS event_log ("
                     "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "  ts INTEGER NOT NULL,"
                     "  type TEXT NOT NULL,"
                     "  detail TEXT,"
                     "  source TEXT"
                     ");")) {
        return 0;
    }
    if (!exec_simple(db, "CREATE INDEX IF NOT EXISTS idx_event_log_ts ON event_log(ts);")) {
        return 0;
    }
    if (!exec_simple(db,
                     "CREATE TABLE IF NOT EXISTS config ("
                     "  key TEXT PRIMARY KEY,"
                     "  value TEXT NOT NULL"
                     ");")) {
        return 0;
    }
    return 1;
}

static int config_exists(hub_db_t *db, const char *key)
{
    sqlite3_stmt *st = NULL;
    int exists = 0;
    if (sqlite3_prepare_v2(db->db, "SELECT 1 FROM config WHERE key=? LIMIT 1", -1, &st, NULL)
        != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        exists = 1;
    }
    sqlite3_finalize(st);
    return exists;
}

static int seed_defaults(hub_db_t *db)
{
    static const struct {
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
    char buf[256];

    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); ++i) {
        if (!config_exists(db, defaults[i].key)) {
            if (!hub_db_set_config(db, defaults[i].key, defaults[i].value)) {
                return 0;
            }
        }
    }

    /* 旧版默认值迁移 */
    if (hub_db_get_config(db, "mqtt_host", buf, sizeof(buf), "")
        && strcmp(buf, "bemfa.com") == 0) {
        hub_db_set_config(db, "mqtt_host", "mqtt.bemfa.com");
    }
    if (hub_db_get_config(db, "mqtt_cmd_topic", buf, sizeof(buf), "") && buf[0] == '\0') {
        hub_db_set_config(db, "mqtt_cmd_topic", "rk3566cmd");
    }
    if (hub_db_get_config(db, "mqtt_event_topic", buf, sizeof(buf), "") && buf[0] == '\0') {
        hub_db_set_config(db, "mqtt_event_topic", "rk3566event");
    }
    return 1;
}

hub_db_t *hub_db_open(const char *db_path)
{
    hub_db_t *h = NULL;
    int rc;

    if (!db_path || !db_path[0]) {
        return NULL;
    }
    if (ensure_parent_dir(db_path) != 0) {
        fprintf(stderr, "hub_db: cannot create parent dir for %s\n", db_path);
        return NULL;
    }

    h = (hub_db_t *)calloc(1, sizeof(*h));
    if (!h) {
        return NULL;
    }

    rc = sqlite3_open(db_path, &h->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "hub_db: open failed: %s\n",
                h->db ? sqlite3_errmsg(h->db) : "unknown");
        if (h->db) {
            sqlite3_close(h->db);
        }
        free(h);
        return NULL;
    }

    exec_simple(h->db, "PRAGMA journal_mode=WAL;");
    exec_simple(h->db, "PRAGMA synchronous=NORMAL;");

    if (!ensure_schema(h->db) || !seed_defaults(h)) {
        hub_db_close(h);
        return NULL;
    }

    fprintf(stderr, "hub_db: SQLite ready: %s\n", db_path);
    return h;
}

void hub_db_close(hub_db_t *db)
{
    if (!db) {
        return;
    }
    if (db->db) {
        sqlite3_close(db->db);
        db->db = NULL;
    }
    free(db);
}

int hub_db_is_open(const hub_db_t *db)
{
    return db && db->db;
}

int hub_db_log_event(hub_db_t *db, const char *type, const char *detail, const char *source)
{
    sqlite3_stmt *st = NULL;
    int rc;

    if (!hub_db_is_open(db) || !type) {
        return 0;
    }
    if (!detail) {
        detail = "";
    }
    if (!source) {
        source = "system";
    }

    if (sqlite3_prepare_v2(db->db,
                           "INSERT INTO event_log(ts, type, detail, source) VALUES(?,?,?,?)",
                           -1, &st, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "hub_db logEvent prepare: %s\n", sqlite3_errmsg(db->db));
        return 0;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)time(NULL));
    sqlite3_bind_text(st, 2, type, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, detail, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, source, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "hub_db logEvent: %s\n", sqlite3_errmsg(db->db));
        return 0;
    }
    return 1;
}

int hub_db_get_config(const hub_db_t *db, const char *key, char *out, size_t out_sz,
                      const char *default_value)
{
    sqlite3_stmt *st = NULL;
    int ok = 0;

    if (!out || out_sz == 0 || !key) {
        return 0;
    }
    out[0] = '\0';
    if (!default_value) {
        default_value = "";
    }

    if (!hub_db_is_open(db)) {
        snprintf(out, out_sz, "%s", default_value);
        return 1;
    }

    if (sqlite3_prepare_v2(db->db, "SELECT value FROM config WHERE key=?", -1, &st, NULL)
        != SQLITE_OK) {
        snprintf(out, out_sz, "%s", default_value);
        return 1;
    }
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *v = sqlite3_column_text(st, 0);
        snprintf(out, out_sz, "%s", v ? (const char *)v : default_value);
        ok = 1;
    } else {
        snprintf(out, out_sz, "%s", default_value);
        ok = 1;
    }
    sqlite3_finalize(st);
    return ok;
}

int hub_db_set_config(hub_db_t *db, const char *key, const char *value)
{
    sqlite3_stmt *st = NULL;
    int rc;

    if (!hub_db_is_open(db) || !key || !value) {
        return 0;
    }
    if (sqlite3_prepare_v2(db->db,
                           "INSERT INTO config(key, value) VALUES(?, ?) "
                           "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                           -1, &st, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "hub_db setConfig prepare: %s\n", sqlite3_errmsg(db->db));
        return 0;
    }
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, value, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "hub_db setConfig: %s\n", sqlite3_errmsg(db->db));
        return 0;
    }
    return 1;
}

int hub_db_purge_events_older_than(hub_db_t *db, int64_t cutoff_unix_sec)
{
    sqlite3_stmt *st = NULL;
    int rc;
    int changes;

    if (!hub_db_is_open(db)) {
        return 0;
    }
    if (sqlite3_prepare_v2(db->db, "DELETE FROM event_log WHERE ts < ?", -1, &st, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "hub_db purge prepare: %s\n", sqlite3_errmsg(db->db));
        return -1;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)cutoff_unix_sec);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "hub_db purge: %s\n", sqlite3_errmsg(db->db));
        return -1;
    }
    changes = sqlite3_changes(db->db);
    return changes;
}
