#ifndef NET_WIFI_TWT_H
#define NET_WIFI_TWT_H

#include "contract_p3_wifi.h"

fl_result_t fl_net_wifi_twt_negotiate(const fl_net_wifi_twt_params_t *req,
                                      fl_net_wifi_twt_params_t *agreed_out);

fl_result_t fl_net_wifi_twt_lab_teardown(uint8_t flow_id);

void fl_net_wifi_twt_lab_reset(void);

/** Microseconds until the next negotiated TWT service period (0 = none / wake now). */
uint64_t fl_net_wifi_twt_next_wake_us(void);

/** Non-zero when the STA should sleep until the next service-period boundary. */
int fl_net_wifi_twt_should_sleep(void);

/** Register agreed TWT for power-manager sleep scheduling between SPs. */
void fl_net_wifi_twt_power_schedule(const fl_net_wifi_twt_params_t *agreed);

/** Sleep until the next SP boundary (capped). No-op when already awake / no schedule. */
fl_result_t fl_net_wifi_twt_power_sleep(void);

#endif /* NET_WIFI_TWT_H */
