#include "net_wifi_db.h"

#include "password_hash.h"
#include "timekeeping.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static sqlite3 *s_wifi_db;

static fl_result_t db_exec(sqlite3 *sql, const char *stmt) {
    char *err = NULL;
    if (sqlite3_exec(sql, stmt, NULL, NULL, &err) != SQLITE_OK) {
        if (err) {
            fprintf(stderr, "wifi_db: %s\n", err);
            sqlite3_free(err);
        }
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

static fl_result_t db_init_schema(sqlite3 *sql) {
    static const char *schema =
        "CREATE TABLE IF NOT EXISTS wifi_router ("
        "  name TEXT PRIMARY KEY NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  salt_hex TEXT NOT NULL,"
        "  bssid_hex TEXT NOT NULL DEFAULT '',"
        "  auth_mode INTEGER NOT NULL DEFAULT 0,"
        "  band INTEGER NOT NULL DEFAULT 0,"
        "  channel INTEGER NOT NULL DEFAULT 0,"
        "  channel_width_mhz INTEGER NOT NULL DEFAULT 0,"
        "  he_supported INTEGER NOT NULL DEFAULT 0,"
        "  bss_color INTEGER NOT NULL DEFAULT 0,"
        "  twt_responder INTEGER NOT NULL DEFAULT 0,"
        "  rssi_dbm INTEGER NOT NULL DEFAULT 0,"
        "  rssi_5ghz_dbm INTEGER NOT NULL DEFAULT 0,"
        "  last_joined_at INTEGER NOT NULL DEFAULT 0,"
        "  is_joined INTEGER NOT NULL DEFAULT 0"
        ");";
    return db_exec(sql, schema);
}

static void bssid_to_hex(const uint8_t bssid[6], char *out, size_t out_cap) {
    size_t i;
    if (!out || out_cap < FL_WIFI_DB_BSSID_HEX_MAX)
        return;
    for (i = 0; i < 6u; i++)
        snprintf(out + i * 2u, out_cap - i * 2u, "%02x", bssid[i]);
}

static void fill_router_from_stmt(sqlite3_stmt *st, fl_wifi_db_router_t *out) {
    const char *txt;

    memset(out, 0, sizeof(*out));
    txt = (const char *)sqlite3_column_text(st, 0);
    if (txt)
        strncpy(out->name, txt, sizeof(out->name) - 1u);
    txt = (const char *)sqlite3_column_text(st, 1);
    if (txt)
        strncpy(out->password_hash, txt, sizeof(out->password_hash) - 1u);
    txt = (const char *)sqlite3_column_text(st, 2);
    if (txt)
        strncpy(out->salt_hex, txt, sizeof(out->salt_hex) - 1u);
    txt = (const char *)sqlite3_column_text(st, 3);
    if (txt)
        strncpy(out->bssid_hex, txt, sizeof(out->bssid_hex) - 1u);
    out->auth_mode = (uint8_t)sqlite3_column_int(st, 4);
    out->band = (uint8_t)sqlite3_column_int(st, 5);
    out->channel = (uint8_t)sqlite3_column_int(st, 6);
    out->channel_width_mhz = (uint8_t)sqlite3_column_int(st, 7);
    out->he_supported = (uint8_t)sqlite3_column_int(st, 8);
    out->bss_color = (uint8_t)sqlite3_column_int(st, 9);
    out->twt_responder = (uint8_t)sqlite3_column_int(st, 10);
    out->rssi_dbm = (int8_t)sqlite3_column_int(st, 11);
    out->rssi_5ghz_dbm = (int8_t)sqlite3_column_int(st, 12);
    out->last_joined_at = (uint64_t)sqlite3_column_int64(st, 13);
    out->is_joined = (uint8_t)sqlite3_column_int(st, 14);
}

static void wifi_db_ensure_parent_dir(const char *db_path) {
    char dir[512];
    size_t i;
    size_t len;

    if (!db_path || !db_path[0] || db_path[0] == ':')
        return;
    strncpy(dir, db_path, sizeof(dir) - 1u);
    dir[sizeof(dir) - 1u] = '\0';
    len = strlen(dir);
    for (i = len; i > 0u; i--) {
        if (dir[i - 1u] == '/') {
            dir[i - 1u] = '\0';
            break;
        }
    }
    if (!dir[0] || !strcmp(dir, ".") || !strcmp(dir, "/"))
        return;
    len = strlen(dir);
    for (i = 1u; i < len; i++) {
        if (dir[i] != '/')
            continue;
        dir[i] = '\0';
        (void)mkdir(dir, 0700);
        dir[i] = '/';
    }
    (void)mkdir(dir, 0700);
}

static const char *wifi_db_resolve_default_path(void) {
    static char path[512];
    const char *home = getenv("HOME");

    if (home && home[0]) {
        snprintf(path, sizeof(path), "%s/.local/share/BPForbes_Flinstone_Shell/fl_wifi.db",
                 home);
        return path;
    }
    return "/tmp/fl_wifi.db";
}

fl_result_t fl_wifi_db_open(const char *path) {
    const char *env = getenv("FL_WIFI_DB_PATH");
    const char *db_path = path && path[0] ? path : (env && env[0] ? env : NULL);
    const char *fallback = wifi_db_resolve_default_path();

    if (s_wifi_db)
        return FL_RESULT_OK;
    if (!db_path || !db_path[0])
        db_path = FL_WIFI_DB_PATH_DEFAULT;
    wifi_db_ensure_parent_dir(db_path);
    if (sqlite3_open(db_path, &s_wifi_db) != SQLITE_OK) {
        s_wifi_db = NULL;
        wifi_db_ensure_parent_dir(fallback);
        if (sqlite3_open(fallback, &s_wifi_db) != SQLITE_OK) {
            s_wifi_db = NULL;
            return FL_RESULT_ERR;
        }
    }
    return db_init_schema(s_wifi_db);
}

void fl_wifi_db_close(void) {
    if (!s_wifi_db)
        return;
    sqlite3_close(s_wifi_db);
    s_wifi_db = NULL;
}

fl_result_t fl_wifi_db_upsert_router(const fl_wifi_db_router_t *row) {
    sqlite3_stmt *st = NULL;
    const char *ins =
        "INSERT INTO wifi_router("
        "name,password_hash,salt_hex,bssid_hex,auth_mode,band,channel,"
        "channel_width_mhz,he_supported,bss_color,twt_responder,"
        "rssi_dbm,rssi_5ghz_dbm,last_joined_at,is_joined) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(name) DO UPDATE SET "
        "password_hash=excluded.password_hash,"
        "salt_hex=excluded.salt_hex,"
        "bssid_hex=excluded.bssid_hex,"
        "auth_mode=excluded.auth_mode,"
        "band=excluded.band,"
        "channel=excluded.channel,"
        "channel_width_mhz=excluded.channel_width_mhz,"
        "he_supported=excluded.he_supported,"
        "bss_color=excluded.bss_color,"
        "twt_responder=excluded.twt_responder,"
        "rssi_dbm=excluded.rssi_dbm,"
        "rssi_5ghz_dbm=excluded.rssi_5ghz_dbm,"
        "last_joined_at=excluded.last_joined_at,"
        "is_joined=excluded.is_joined;";

    if (!row || !row->name[0] || !s_wifi_db)
        return FL_RESULT_INVAL;
    if (sqlite3_prepare_v2(s_wifi_db, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, row->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, row->password_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, row->salt_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, row->bssid_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 5, (int)row->auth_mode);
    sqlite3_bind_int(st, 6, (int)row->band);
    sqlite3_bind_int(st, 7, (int)row->channel);
    sqlite3_bind_int(st, 8, (int)row->channel_width_mhz);
    sqlite3_bind_int(st, 9, (int)row->he_supported);
    sqlite3_bind_int(st, 10, (int)row->bss_color);
    sqlite3_bind_int(st, 11, (int)row->twt_responder);
    sqlite3_bind_int(st, 12, (int)row->rssi_dbm);
    sqlite3_bind_int(st, 13, (int)row->rssi_5ghz_dbm);
    sqlite3_bind_int64(st, 14, (sqlite3_int64)row->last_joined_at);
    sqlite3_bind_int(st, 15, row->is_joined ? 1 : 0);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

fl_result_t fl_wifi_db_set_password(const char *name, const char *password) {
    fl_wifi_db_router_t row;
    char hash[FL_WIFI_DB_HASH_MAX];
    char salt[FL_WIFI_DB_SALT_MAX];

    if (!name || !name[0] || !password || !s_wifi_db)
        return FL_RESULT_INVAL;
    memset(&row, 0, sizeof(row));
    if (fl_wifi_db_find(name, &row) != FL_RESULT_OK) {
        memset(&row, 0, sizeof(row));
        strncpy(row.name, name, sizeof(row.name) - 1u);
    }
    if (fl_password_generate_salt_hex(salt, sizeof(salt)) != 0)
        return FL_RESULT_ERR;
    if (fl_password_hash_password(password, salt, hash, sizeof(hash)) != 0)
        return FL_RESULT_ERR;
    strncpy(row.password_hash, hash, sizeof(row.password_hash) - 1u);
    strncpy(row.salt_hex, salt, sizeof(row.salt_hex) - 1u);
    return fl_wifi_db_upsert_router(&row);
}

fl_result_t fl_wifi_db_apply_scan_entry(const char *name,
                                        const fl_net_wifi_scan_entry_t *scan) {
    fl_wifi_db_router_t row;

    if (!name || !scan || !s_wifi_db)
        return FL_RESULT_INVAL;
    memset(&row, 0, sizeof(row));
    if (fl_wifi_db_find(name, &row) != FL_RESULT_OK) {
        memset(&row, 0, sizeof(row));
        strncpy(row.name, name, sizeof(row.name) - 1u);
        strncpy(row.password_hash, "-", sizeof(row.password_hash) - 1u);
        strncpy(row.salt_hex, "-", sizeof(row.salt_hex) - 1u);
    }
    bssid_to_hex(scan->bssid, row.bssid_hex, sizeof(row.bssid_hex));
    row.auth_mode = scan->auth_mode;
    row.band = scan->band;
    row.channel = scan->channel;
    row.channel_width_mhz = scan->channel_width_mhz;
    row.he_supported = scan->he_supported;
    row.bss_color = scan->bss_color;
    row.twt_responder = scan->twt_responder;
    row.rssi_dbm = scan->rssi_dbm;
    row.rssi_5ghz_dbm = scan->rssi_5ghz_dbm;
    return fl_wifi_db_upsert_router(&row);
}

fl_result_t fl_wifi_db_mark_joined(const char *name, int joined) {
    fl_wifi_db_router_t row;
    int64_t now_sec = 0;

    if (!name || !name[0] || !s_wifi_db)
        return FL_RESULT_INVAL;
    if (joined) {
        (void)db_exec(s_wifi_db, "UPDATE wifi_router SET is_joined=0;");
        if (fl_wifi_db_find(name, &row) != FL_RESULT_OK) {
            memset(&row, 0, sizeof(row));
            strncpy(row.name, name, sizeof(row.name) - 1u);
        }
        row.is_joined = 1u;
        if (fl_time_wall_sec(&now_sec) == FL_RESULT_OK)
            row.last_joined_at = (uint64_t)now_sec;
        return fl_wifi_db_upsert_router(&row);
    }
    if (fl_wifi_db_find(name, &row) != FL_RESULT_OK)
        return FL_RESULT_NOENT;
    row.is_joined = 0u;
    return fl_wifi_db_upsert_router(&row);
}

fl_result_t fl_wifi_db_list(fl_wifi_db_router_t *rows, size_t cap, size_t *count_out) {
    sqlite3_stmt *st = NULL;
    const char *q =
        "SELECT name,password_hash,salt_hex,bssid_hex,auth_mode,band,channel,"
        "channel_width_mhz,he_supported,bss_color,twt_responder,"
        "rssi_dbm,rssi_5ghz_dbm,last_joined_at,is_joined "
        "FROM wifi_router ORDER BY is_joined DESC, last_joined_at DESC, name ASC;";
    size_t n = 0;

    if (!rows || !count_out || cap == 0u || !s_wifi_db)
        return FL_RESULT_INVAL;
    *count_out = 0u;
    if (sqlite3_prepare_v2(s_wifi_db, q, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n >= cap)
            break;
        fill_router_from_stmt(st, &rows[n]);
        n++;
    }
    sqlite3_finalize(st);
    *count_out = n;
    return FL_RESULT_OK;
}

fl_result_t fl_wifi_db_find(const char *name, fl_wifi_db_router_t *out) {
    sqlite3_stmt *st = NULL;
    const char *q =
        "SELECT name,password_hash,salt_hex,bssid_hex,auth_mode,band,channel,"
        "channel_width_mhz,he_supported,bss_color,twt_responder,"
        "rssi_dbm,rssi_5ghz_dbm,last_joined_at,is_joined "
        "FROM wifi_router WHERE name=? LIMIT 1;";

    if (!name || !out || !s_wifi_db)
        return FL_RESULT_INVAL;
    if (sqlite3_prepare_v2(s_wifi_db, q, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        return FL_RESULT_NOENT;
    }
    fill_router_from_stmt(st, out);
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}
