/*
 * Phase 4 station connect: orchestrate in-tree supplicant with FullMAC ops.
 */

#include "wifi_fullmac.h"

#include "net_wifi_mgmt.h"

#include <string.h>

int wifi_fullmac_station_connect(wifi_fullmac_t *dev, const fl_net_wifi_cred_t *cred)
{
	wifi_network_t networks[32];
	uint16_t count = 32;
	size_t i;
	const wifi_network_t *ap = NULL;
	wifi_auth_mode_t auth;

	if (!dev || !cred || !cred->ssid[0] || !dev->ops)
		return -1;
	if (dev->state < WIFI_FULLMAC_STATE_IDLE) {
		wifi_fullmac_set_error("FullMAC not ready for connect");
		return -1;
	}
	if (dev->ops->start_scan(dev, NULL) != 0)
		return -1;
	if (dev->ops->get_scan_results(dev, networks, &count) != 0)
		return -1;
	for (i = 0; i < (size_t)count; i++) {
		if (!strcmp(networks[i].ssid, cred->ssid)) {
			ap = &networks[i];
			break;
		}
	}
	if (!ap) {
		wifi_fullmac_set_error("SSID not found in FullMAC scan results");
		return -1;
	}
	switch (cred->auth_mode ? cred->auth_mode : FL_WIFI_AUTH_WPA2_PSK) {
	case FL_WIFI_AUTH_OPEN:
		auth = WIFI_AUTH_OPEN;
		break;
	case FL_WIFI_AUTH_WPA3_SAE:
		auth = WIFI_AUTH_WPA3_SAE;
		break;
	default:
		auth = WIFI_AUTH_WPA2_PSK;
		break;
	}
	if (auth != WIFI_AUTH_OPEN && cred->passphrase[0] == '\0') {
		wifi_fullmac_set_error("passphrase required");
		return -1;
	}
	if (dev->ops->authenticate &&
	    dev->ops->authenticate(dev, ap->bssid, (uint16_t)auth, 1) != 0)
		return -1;
	if (dev->ops->associate && dev->ops->associate(dev, ap->bssid) != 0)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_CONNECTED;
	return 0;
}
