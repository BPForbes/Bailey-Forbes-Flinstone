#ifndef NET_WIFI_TWT_H
#define NET_WIFI_TWT_H

#include "contract_p3_wifi.h"

fl_result_t fl_net_wifi_twt_negotiate(const fl_net_wifi_twt_params_t *req,
                                      fl_net_wifi_twt_params_t *agreed_out);

fl_result_t fl_net_wifi_twt_teardown(uint8_t flow_id);

void fl_net_wifi_twt_lab_reset(void);

#endif /* NET_WIFI_TWT_H */
