#ifndef KERNEL_DRIVERS_WIFI_LAB_BACKEND_H
#define KERNEL_DRIVERS_WIFI_LAB_BACKEND_H

#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "net_wifi_he.h"
#include "wifi_fullmac.h"

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

/*
 * Software FullMAC mock (FL_WIFI_80211AX_MOCK=1) — in-process WiFi 6 NIC for #279
 * when RF is unavailable. Lives in this file with the hosted lab backend.
 */
int wifi_lab_mock_attach(wifi_fullmac_t **out_dev);
void wifi_lab_mock_detach(wifi_fullmac_t *dev);
int wifi_lab_mock_connect(wifi_fullmac_t *dev, const fl_net_wifi_cred_t *cred);
void wifi_lab_mock_enrich_scan_entry(size_t index, fl_net_wifi_scan_entry_t *entry);
void wifi_lab_mock_fill_he_cap(fl_net_wifi_he_cap_t *cap);

/* Legacy names (pre-consolidation). */
#define wifi_80211ax_mock_attach wifi_lab_mock_attach
#define wifi_80211ax_mock_detach wifi_lab_mock_detach
#define wifi_80211ax_mock_connect wifi_lab_mock_connect
#define wifi_80211ax_mock_enrich_scan_entry wifi_lab_mock_enrich_scan_entry
#define wifi_80211ax_mock_fill_he_cap wifi_lab_mock_fill_he_cap

#endif /* KERNEL_DRIVERS_WIFI_LAB_BACKEND_H */
