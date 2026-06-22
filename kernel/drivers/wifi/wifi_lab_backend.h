#ifndef KERNEL_DRIVERS_WIFI_LAB_BACKEND_H
#define KERNEL_DRIVERS_WIFI_LAB_BACKEND_H

#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "net_wifi_he.h"

/* In-process virtual WiFi NIC for hosted lab (#279, no RF). */

void wifi_lab_reset(void);

fl_result_t wifi_lab_scan(uint8_t band, unsigned timeout_ms);
fl_result_t wifi_lab_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
				 size_t *count_out);

/*
 * Run lab L2 auth/assoc (supplicant, mgmt frames, PTK install) and return the
 * resolved AP row plus negotiated HE capabilities.
 */
fl_result_t wifi_lab_connect(const fl_net_wifi_cred_t *cred,
			     fl_net_wifi_scan_entry_t *ap_out,
			     fl_net_wifi_he_cap_t *he_out);

fl_result_t wifi_lab_he_cap(fl_net_wifi_he_cap_t *cap_out);

#endif /* KERNEL_DRIVERS_WIFI_LAB_BACKEND_H */
