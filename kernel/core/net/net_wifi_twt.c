#include "net_wifi_twt.h"

#include <string.h>

#define FL_NET_WIFI_HOSTED_LAB 1

static fl_net_wifi_twt_params_t s_active_twt[8];
static uint8_t s_active_mask;
static uint64_t s_next_wake_us;

fl_result_t fl_net_wifi_twt_negotiate(const fl_net_wifi_twt_params_t *req,
                                      fl_net_wifi_twt_params_t *agreed_out) {
#if !defined(FL_NET_WIFI_HOSTED_LAB)
    (void)req;
    (void)agreed_out;
    return FL_RESULT_NOSYS;
#else
    uint8_t id;

    if (!req || !agreed_out)
        return FL_RESULT_INVAL;
    if (req->wake_duration_us == 0u || req->wake_interval_us == 0u)
        return FL_RESULT_INVAL;

    for (id = 0; id < 8u; id++) {
        if ((s_active_mask & (1u << id)) == 0u)
            break;
    }
    if (id >= 8u)
        return FL_RESULT_ERR;

    memset(agreed_out, 0, sizeof(*agreed_out));
    *agreed_out = *req;
    agreed_out->flow_id = id;
    if (agreed_out->wake_interval_us < agreed_out->wake_duration_us)
        agreed_out->wake_interval_us = agreed_out->wake_duration_us * 2u;
    s_active_twt[id] = *agreed_out;
    s_active_mask |= (1u << id);
    fl_net_wifi_twt_power_schedule(agreed_out);
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_wifi_twt_lab_teardown(uint8_t flow_id) {
#if !defined(FL_NET_WIFI_HOSTED_LAB)
    (void)flow_id;
    return FL_RESULT_NOSYS;
#else
    if (flow_id >= 8u)
        return FL_RESULT_INVAL;
    if ((s_active_mask & (1u << flow_id)) == 0u)
        return FL_RESULT_NOENT;
    memset(&s_active_twt[flow_id], 0, sizeof(s_active_twt[flow_id]));
    s_active_mask &= (uint8_t)~(1u << flow_id);
    return FL_RESULT_OK;
#endif
}

void fl_net_wifi_twt_lab_reset(void) {
    memset(s_active_twt, 0, sizeof(s_active_twt));
    s_active_mask = 0u;
    s_next_wake_us = 0u;
}

uint64_t fl_net_wifi_twt_next_wake_us(void) {
    return s_next_wake_us;
}

void fl_net_wifi_twt_power_schedule(const fl_net_wifi_twt_params_t *agreed) {
    if (!agreed || agreed->wake_interval_us == 0u)
        return;
    s_next_wake_us = (uint64_t)agreed->wake_interval_us;
}
