#ifndef NET_WIFI_STATION_H
#define NET_WIFI_STATION_H

#include "contract_p3_wifi.h"
#include "fl/driver/net.h"

/**
 * Station API for **#279** (802.11ax). Without a registered P4/NIC backend, scan,
 * connect, and TWT calls return **FL_RESULT_NOSYS**; **fl_net_wifi_station_netdev**
 * returns NULL.
 */

fl_result_t fl_net_wifi_station_init(void);
fl_net_driver_t *fl_net_wifi_station_netdev(void);

int fl_net_wifi_station_host_backend(void);

/** Non-zero when the last scan/connect path used in-tree lab data (no host NIC). */
int fl_net_wifi_station_lab_backend(void);

fl_result_t fl_net_wifi_scan(uint8_t band, unsigned timeout_ms);
fl_result_t fl_net_wifi_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                    size_t *count_out);

fl_result_t fl_net_wifi_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms);
fl_result_t fl_net_wifi_disconnect(void);

fl_net_wifi_state_t fl_net_wifi_state(void);
fl_result_t fl_net_wifi_he_cap(fl_net_wifi_he_cap_t *cap_out);

fl_result_t fl_net_wifi_twt_setup(const fl_net_wifi_twt_params_t *req,
                                  fl_net_wifi_twt_params_t *agreed_out);
fl_result_t fl_net_wifi_twt_teardown(uint8_t flow_id);

/** Zero ephemeral passphrase bytes after use (P2/P5 owns long-term storage). */
void fl_net_wifi_cred_scrub_passphrase(fl_net_wifi_cred_t *cred);

#endif /* NET_WIFI_STATION_H */
