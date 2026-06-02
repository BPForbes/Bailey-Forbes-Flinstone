#ifndef NET_WIFI_MGMT_H
#define NET_WIFI_MGMT_H

#include <stddef.h>
#include <stdint.h>

/** 802.11 management frame type/subtype: Probe Request (0x04). */
#define FL_WIFI_MGMT_PROBE_REQ 0x04u

/**
 * Minimum length check for a management frame header (FC + Duration + DA + SA + BSSID + Seq).
 */
int fl_net_wifi_mgmt_hdr_valid(const uint8_t *frame, size_t len);

#endif /* NET_WIFI_MGMT_H */
