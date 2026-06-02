#ifndef NET_WIFI_WPA_H
#define NET_WIFI_WPA_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/** WPA2 4-way handshake — **#279** tail; **FL_RESULT_NOSYS** without backend. */
fl_result_t fl_net_wifi_wpa4_install_ptk(const uint8_t *pmk, size_t pmk_len);

#endif /* NET_WIFI_WPA_H */
