#ifndef NET_WIFI_TWT_H
#define NET_WIFI_TWT_H

#include "contract_p3_wifi.h"

/** TWT individual negotiation — **#279** tail; needs associated NIC. */
fl_result_t fl_net_wifi_twt_negotiate(const fl_net_wifi_twt_params_t *req,
                                      fl_net_wifi_twt_params_t *agreed_out);

#endif /* NET_WIFI_TWT_H */
