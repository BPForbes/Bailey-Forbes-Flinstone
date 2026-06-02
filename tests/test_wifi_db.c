#include "net_wifi_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int test_wifi_db_roundtrip(void) {
    fl_wifi_db_router_t row;
    fl_wifi_db_router_t listed[8];
    size_t n = 0;
    fl_net_wifi_scan_entry_t scan;

    if (!getenv("FL_WIFI_DB_PATH"))
        setenv("FL_WIFI_DB_PATH", "/tmp/fl_test_wifi.db", 0);
    ASSERT(fl_wifi_db_open(NULL) == FL_RESULT_OK);
    ASSERT(fl_wifi_db_set_password("LabAxHome", "secret") == FL_RESULT_OK);
    memset(&scan, 0, sizeof(scan));
    strncpy(scan.ssid, "LabAxHome", sizeof(scan.ssid) - 1u);
    scan.bssid[0] = 0x02;
    scan.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    scan.he_supported = 1;
    scan.bss_color = 5;
    scan.channel_width_mhz = 80;
    ASSERT(fl_wifi_db_apply_scan_entry("LabAxHome", &scan) == FL_RESULT_OK);
    ASSERT(fl_wifi_db_mark_joined("LabAxHome", 1) == FL_RESULT_OK);
    ASSERT(fl_wifi_db_find("LabAxHome", &row) == FL_RESULT_OK);
    ASSERT(row.he_supported == 1u);
    ASSERT(row.bss_color == 5u);
    ASSERT(strcmp(row.password_hash, "-") != 0);
    ASSERT(fl_wifi_db_list(listed, 8, &n) == FL_RESULT_OK);
    ASSERT(n >= 1u);
    fl_wifi_db_close();
    return 0;
}

int main(void) {
    if (test_wifi_db_roundtrip() != 0)
        return 1;
    puts("test_wifi_db: all passed");
    return 0;
}
