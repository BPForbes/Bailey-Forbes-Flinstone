/*
 * Software 802.11ax (WiFi 6) FullMAC mock for hosted CI and Wi-Fi 5-only hardware.
 * Satisfies issue #279 prerequisites 1–2 and acceptance 20–21 in mock mode when
 * FL_WIFI_80211AX_MOCK=1 (no RF; simulates ax NIC + OTA auth handshakes).
 */

#ifndef KERNEL_DRIVERS_WIFI_80211AX_MOCK_H
#define KERNEL_DRIVERS_WIFI_80211AX_MOCK_H

#include "contract_p3_wifi.h"
#include "wifi_fullmac.h"

/** Attach mock FullMAC device; sets **out_dev** and registers mock ops. */
int wifi_80211ax_mock_attach(wifi_fullmac_t **out_dev);

void wifi_80211ax_mock_detach(wifi_fullmac_t *dev);

/** Association + supplicant mock OTA (WPA3-SAE / WPA2-PSK / open). */
int wifi_80211ax_mock_connect(wifi_fullmac_t *dev, const fl_net_wifi_cred_t *cred);

/** Fill HE / band fields on scan rows produced by the mock driver backend. */
void wifi_80211ax_mock_enrich_scan_entry(size_t index, fl_net_wifi_scan_entry_t *entry);

/** Fill negotiated HE caps after mock connect (6 GHz ax vs legacy AC). */
void wifi_80211ax_mock_fill_he_cap(fl_net_wifi_he_cap_t *cap);

#endif /* KERNEL_DRIVERS_WIFI_80211AX_MOCK_H */
