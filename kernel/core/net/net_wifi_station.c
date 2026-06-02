#include "net_wifi_station.h"

#include "net_wifi_he.h"

#include <string.h>

static fl_net_wifi_state_t s_wifi_state = FL_WIFI_STATE_IDLE;
static fl_net_wifi_he_cap_t s_negotiated_he;

fl_result_t fl_net_wifi_station_init(void) {
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
    s_wifi_state = FL_WIFI_STATE_IDLE;
    return FL_RESULT_OK;
}

fl_net_driver_t *fl_net_wifi_station_netdev(void) {
    return NULL;
}

fl_result_t fl_net_wifi_scan(uint8_t band, unsigned timeout_ms) {
    (void)band;
    (void)timeout_ms;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                    size_t *count_out) {
    if (!entries || !count_out || cap == 0u)
        return FL_RESULT_INVAL;
    *count_out = 0u;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms) {
    if (!cred)
        return FL_RESULT_INVAL;
    (void)timeout_ms;
    s_wifi_state = FL_WIFI_STATE_ERROR;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_disconnect(void) {
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
    s_wifi_state = FL_WIFI_STATE_IDLE;
    return FL_RESULT_OK;
}

fl_net_wifi_state_t fl_net_wifi_state(void) {
    return s_wifi_state;
}

fl_result_t fl_net_wifi_he_cap(fl_net_wifi_he_cap_t *cap_out) {
    if (!cap_out)
        return FL_RESULT_INVAL;
    if (s_wifi_state != FL_WIFI_STATE_CONNECTED && s_wifi_state != FL_WIFI_STATE_UP &&
        s_wifi_state != FL_WIFI_STATE_DHCP)
        return FL_RESULT_ERR;
    *cap_out = s_negotiated_he;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_twt_setup(const fl_net_wifi_twt_params_t *req,
                                  fl_net_wifi_twt_params_t *agreed_out) {
    (void)req;
    (void)agreed_out;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_twt_teardown(uint8_t flow_id) {
    (void)flow_id;
    return FL_RESULT_NOSYS;
}
