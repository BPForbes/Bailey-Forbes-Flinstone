#ifndef NET_WIFI_DB_H
#define NET_WIFI_DB_H

#include "contract_p3_wifi.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#define FL_WIFI_DB_PATH_DEFAULT "userland/shell/fl_wifi.db"
#define FL_WIFI_DB_NAME_MAX FL_WIFI_SSID_MAX
#define FL_WIFI_DB_BSSID_HEX_MAX 13u
#define FL_WIFI_DB_HASH_MAX 96u
#define FL_WIFI_DB_SALT_MAX 33u
#define FL_WIFI_DB_MAX_ROUTERS 64u

typedef struct {
    char name[FL_WIFI_DB_NAME_MAX];
    char password_hash[FL_WIFI_DB_HASH_MAX];
    char salt_hex[FL_WIFI_DB_SALT_MAX];
    char bssid_hex[FL_WIFI_DB_BSSID_HEX_MAX];
    uint8_t auth_mode;
    uint8_t band;
    uint8_t channel;
    uint8_t channel_width_mhz;
    uint8_t he_supported;
    uint8_t bss_color;
    uint8_t twt_responder;
    int8_t rssi_dbm;
    int8_t rssi_5ghz_dbm;
    uint64_t last_joined_at;
    uint8_t is_joined;
} fl_wifi_db_router_t;

fl_result_t fl_wifi_db_open(const char *path);
void fl_wifi_db_close(void);

fl_result_t fl_wifi_db_upsert_router(const fl_wifi_db_router_t *row);
fl_result_t fl_wifi_db_set_password(const char *name, const char *password);
fl_result_t fl_wifi_db_apply_scan_entry(const char *name, const fl_net_wifi_scan_entry_t *scan);
fl_result_t fl_wifi_db_mark_joined(const char *name, int joined);

fl_result_t fl_wifi_db_list(fl_wifi_db_router_t *rows, size_t cap, size_t *count_out);
fl_result_t fl_wifi_db_find(const char *name, fl_wifi_db_router_t *out);

#endif /* NET_WIFI_DB_H */
