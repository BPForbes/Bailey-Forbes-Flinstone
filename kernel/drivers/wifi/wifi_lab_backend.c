/*
 * Lab virtual WiFi driver — scan cache, supplicant, mgmt/auth/assoc, PTK.
 * Execution layer for hosted simulation; net_wifi_station orchestrates only.
 */

#include "wifi_lab_backend.h"

#include "wifi_lab_router.h"
#include "wifi_supplicant.h"
#include "wifi_driver_packet.h"
#include "wifi_connect_ota.h"
#include "wifi_mgmt_transport.h"
#include "wifi_twt_ota.h"

#include "net_loopback.h"
#include "net_wifi_he.h"
#include "net_wifi_crypto.h"
#include "net_wire.h"
#include "net_wifi_mgmt.h"
#include "net_wifi_sae.h"
#include "net_wifi_twt.h"
#include "net_wifi_wpa.h"
#include "net_checksum.h"
#include "net_udp.h"

#include "fl/mem_asm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fl_net_wifi_scan_entry_t s_lab_scan[8];
static size_t s_lab_scan_count;
static fl_net_wifi_he_cap_t s_negotiated_he;
static int s_lab_connected;

static const uint8_t s_lab_probe_resp_ies[] = {
    FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
    0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
    0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x05
};

/* 802.11ax on 2.4 GHz — HE present without 80/160 MHz PHY widths. */
static const uint8_t s_lab_ax_ies_2g[] = {
    FL_WIFI_ELEM_ID_EXTENSION, 4u, FL_WIFI_EXT_HE_CAPABILITIES, 0x00, 0x80, 0x00,
    FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x07
};

static void lab_apply_ax_ap(fl_net_wifi_scan_entry_t *e)
{
    const uint8_t *ies;
    size_t ies_len;

    if (!e)
        return;
    if (e->band == FL_WIFI_BAND_2GHZ) {
        ies = s_lab_ax_ies_2g;
        ies_len = sizeof(s_lab_ax_ies_2g);
    } else {
        ies = s_lab_probe_resp_ies;
        ies_len = sizeof(s_lab_probe_resp_ies);
    }
    (void)fl_net_wifi_scan_enrich_from_ies(ies, ies_len, e);
    if (e->band == FL_WIFI_BAND_2GHZ) {
        if (e->channel_width_mhz == 0u || e->channel_width_mhz > 40u)
            e->channel_width_mhz = 20u;
    }
}

static void lab_seed_scan(uint8_t band)
{
    s_lab_scan_count = 0;
    memset(s_lab_scan, 0, sizeof(s_lab_scan));

    if (band == FL_WIFI_BAND_6GHZ) {
        fl_net_wifi_scan_entry_t *e = &s_lab_scan[s_lab_scan_count++];

        strncpy(e->ssid, "LabAx6", sizeof(e->ssid) - 1u);
        e->bssid[0] = 0x02;
        e->bssid[1] = 0x66;
        e->bssid[2] = 0x00;
        e->bssid[3] = 0x00;
        e->bssid[4] = 0x00;
        e->bssid[5] = 0x01;
        e->rssi_dbm = -48;
        e->channel = 37;
        e->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
        e->band = FL_WIFI_BAND_6GHZ;
        e->channel_width_mhz = 160;
        lab_apply_ax_ap(e);
        return;
    }

    {
        fl_net_wifi_scan_entry_t *e = &s_lab_scan[s_lab_scan_count++];

        strncpy(e->ssid, "LabAxHome", sizeof(e->ssid) - 1u);
        e->bssid[0] = 0x02;
        e->bssid[1] = 0x11;
        e->bssid[2] = 0x22;
        e->bssid[3] = 0x33;
        e->bssid[4] = 0x44;
        e->bssid[5] = 0x55;
        e->rssi_dbm = -42;
        e->channel = 36;
        e->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
        e->band = FL_WIFI_BAND_5GHZ;
        e->channel_width_mhz = 80;
        lab_apply_ax_ap(e);
    }
    if (band == FL_WIFI_BAND_2GHZ || band == FL_WIFI_BAND_ANY) {
        fl_net_wifi_scan_entry_t *e = &s_lab_scan[s_lab_scan_count++];

        strncpy(e->ssid, "GuestOpen", sizeof(e->ssid) - 1u);
        e->bssid[0] = 0xaa;
        e->bssid[1] = 0xbb;
        e->bssid[2] = 0xcc;
        e->bssid[3] = 0xdd;
        e->bssid[4] = 0xee;
        e->bssid[5] = 0xff;
        e->rssi_dbm = -65;
        e->channel = 6;
        e->auth_mode = FL_WIFI_AUTH_OPEN;
        e->band = FL_WIFI_BAND_2GHZ;
        e->channel_width_mhz = 20;
        lab_apply_ax_ap(e);
    }
    {
        const char *home_ssid = getenv("FL_NET_WIFI_HOME_SSID");

        if (home_ssid && home_ssid[0] && s_lab_scan_count < 8u) {
            const char *home_auth = getenv("FL_NET_WIFI_HOME_AUTH");
            const char *home_band_env = getenv("FL_NET_WIFI_HOME_BAND");
            uint8_t home_band;
            uint8_t home_channel;

            if (home_band_env &&
                (!strcmp(home_band_env, "5") || !strcmp(home_band_env, "5ghz"))) {
                home_band = FL_WIFI_BAND_5GHZ;
                home_channel = 36;
            } else if (home_band_env &&
                       (!strcmp(home_band_env, "6") || !strcmp(home_band_env, "6ghz"))) {
                home_band = FL_WIFI_BAND_6GHZ;
                home_channel = 37;
            } else {
                home_band = FL_WIFI_BAND_2GHZ;
                home_channel = 6;
            }
            if (band == FL_WIFI_BAND_ANY || band == home_band) {
                fl_net_wifi_scan_entry_t *e = &s_lab_scan[s_lab_scan_count++];

                memset(e, 0, sizeof(*e));
                strncpy(e->ssid, home_ssid, sizeof(e->ssid) - 1u);
                e->bssid[0] = 0x02;
                e->bssid[5] = 0xfe;
                e->rssi_dbm = -55;
                e->channel = home_channel;
                e->channel_width_mhz = 20;
                e->band = home_band;
                if (home_auth &&
                    (!strcmp(home_auth, "wpa3") || !strcmp(home_auth, "sae")))
                    e->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
                else if (home_auth && !strcmp(home_auth, "open"))
                    e->auth_mode = FL_WIFI_AUTH_OPEN;
                else
                    e->auth_mode = FL_WIFI_AUTH_WPA2_PSK;
                lab_apply_ax_ap(e);
            }
        }
    }
}

static const fl_net_wifi_scan_entry_t *lab_find_ssid(const char *ssid)
{
    size_t i;

    if (!ssid)
        return NULL;
    for (i = 0; i < s_lab_scan_count; i++) {
        if (!strcmp(s_lab_scan[i].ssid, ssid))
            return &s_lab_scan[i];
    }
    return NULL;
}

static fl_result_t lab_derive_pmk(const fl_net_wifi_cred_t *cred,
				  uint8_t pmk[FL_NET_WIFI_PMK_LEN])
{
    if (cred->auth_mode == FL_WIFI_AUTH_OPEN || cred->auth_mode == FL_WIFI_AUTH_OWE)
        return FL_RESULT_OK;
    if (cred->auth_mode == FL_WIFI_AUTH_WPA3_SAE)
        return fl_net_wifi_sae_derive_pmk(cred->ssid, cred->passphrase, pmk,
                                          FL_NET_WIFI_PMK_LEN);
    if (cred->auth_mode == FL_WIFI_AUTH_WPA2_PSK)
        return fl_net_wifi_wpa_psk_pmk(cred->ssid, cred->passphrase, pmk);
    return FL_RESULT_NOSYS;
}

static fl_result_t lab_supplicant_auth(const fl_net_wifi_cred_t *cred,
				       const fl_net_wifi_scan_entry_t *ap,
				       const uint8_t sta_mac[6],
				       uint8_t pmk[FL_NET_WIFI_PMK_LEN])
{
    wifi_supplicant_t supp;

    if (ap->auth_mode == FL_WIFI_AUTH_OPEN || ap->auth_mode == FL_WIFI_AUTH_OWE)
        return FL_RESULT_OK;

    if (wifi_supplicant_init(&supp, ap->bssid) != 0)
        return FL_RESULT_ERR;
    if (wifi_supplicant_set_credentials(&supp, cred->ssid, cred->passphrase) != 0 ||
        wifi_supplicant_set_sta_addr(&supp, sta_mac) != 0) {
        (void)wifi_supplicant_deinit(&supp);
        return FL_RESULT_ERR;
    }
    if (ap->auth_mode == FL_WIFI_AUTH_WPA3_SAE) {
        if (wifi_supplicant_start_sae_handshake(&supp) != 0 ||
            fl_net_wifi_sae_derive_pmk(cred->ssid, cred->passphrase, supp.keys.pmk,
                                       FL_NET_WIFI_PMK_LEN) != FL_RESULT_OK) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
    } else if (wifi_supplicant_derive_pmk_psk(&supp, cred->ssid, cred->passphrase) != 0 ||
               wifi_supplicant_start_4way_handshake(&supp) != 0) {
        (void)wifi_supplicant_deinit(&supp);
        return FL_RESULT_ERR;
    }
    memcpy(pmk, supp.keys.pmk, FL_NET_WIFI_PMK_LEN);
    (void)wifi_supplicant_deinit(&supp);
    return FL_RESULT_OK;
}

static fl_result_t lab_run_mgmt_assoc(const fl_net_wifi_cred_t *cred,
				      const fl_net_wifi_scan_entry_t *ap,
				      const uint8_t sta_mac[6], uint8_t pmk[FL_NET_WIFI_PMK_LEN])
{
    uint8_t probe[128];
    uint8_t auth[64];
    uint8_t assoc[200];
    size_t frame_len = 0;
    fl_result_t rc;

    if (fl_net_wifi_mgmt_build_probe_req(cred->ssid, sta_mac, probe, sizeof(probe), &frame_len) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_net_wifi_mgmt_build_auth_req(sta_mac, ap->bssid, auth, sizeof(auth), &frame_len) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_net_wifi_mgmt_build_assoc_req(cred->ssid, ap->bssid, sta_mac, ap->auth_mode, NULL,
                                         assoc, sizeof(assoc), &frame_len) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    if (ap->auth_mode != FL_WIFI_AUTH_OPEN && ap->auth_mode != FL_WIFI_AUTH_OWE) {
        rc = fl_net_wifi_wpa4_install_ptk(pmk, FL_NET_WIFI_PMK_LEN);
        if (rc != FL_RESULT_OK)
            return rc;
    }
    (void)probe;
    (void)auth;
    (void)assoc;
    return FL_RESULT_OK;
}

static void lab_fill_he(const fl_net_wifi_scan_entry_t *ap, fl_net_wifi_he_cap_t *he_out)
{
    memset(he_out, 0, sizeof(*he_out));
    if (!ap->he_supported)
        return;
    he_out->supports_ofdma = 1;
    he_out->supports_mu_mimo = 1;
    he_out->supports_twt = ap->twt_responder;
    he_out->bss_color = ap->bss_color;
    he_out->channel_width_mhz = ap->channel_width_mhz ? ap->channel_width_mhz : 20u;
    he_out->max_nss_rx = 2;
    he_out->max_nss_tx = 2;
    he_out->supports_6ghz = (ap->band == FL_WIFI_BAND_6GHZ) ? 1u : 0u;
}

void wifi_lab_reset(void)
{
    s_lab_scan_count = 0;
    memset(s_lab_scan, 0, sizeof(s_lab_scan));
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
    s_lab_connected = 0;
    wifi_lab_router_reset();
    fl_net_wifi_wpa_lab_reset();
}

fl_result_t wifi_lab_scan(uint8_t band, unsigned timeout_ms)
{
    (void)timeout_ms;
    lab_seed_scan(band);
    return FL_RESULT_OK;
}

fl_result_t wifi_lab_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
				 size_t *count_out)
{
    size_t i;

    if (!entries || !count_out || cap == 0u)
        return FL_RESULT_INVAL;
    *count_out = 0;
    for (i = 0; i < s_lab_scan_count && i < cap; i++) {
        entries[i] = s_lab_scan[i];
        (*count_out)++;
    }
    return FL_RESULT_OK;
}

fl_result_t wifi_lab_connect(const fl_net_wifi_cred_t *cred,
			     fl_net_wifi_scan_entry_t *ap_out,
			     fl_net_wifi_he_cap_t *he_out)
{
    const fl_net_wifi_scan_entry_t *ap;
    static fl_net_wifi_scan_entry_t s_synth_ap;
    uint8_t pmk[FL_NET_WIFI_PMK_LEN];
    uint8_t sta_mac[6];
    fl_result_t rc;

    if (!cred || !cred->ssid[0] || !ap_out || !he_out)
        return FL_RESULT_INVAL;

    ap = lab_find_ssid(cred->ssid);
    if (!ap) {
        memset(&s_synth_ap, 0, sizeof(s_synth_ap));
        strncpy(s_synth_ap.ssid, cred->ssid, sizeof(s_synth_ap.ssid) - 1u);
        memcpy(s_synth_ap.bssid, cred->bssid, 6u);
        s_synth_ap.auth_mode = cred->auth_mode ? cred->auth_mode :
                               (cred->passphrase[0] ? FL_WIFI_AUTH_WPA2_PSK
                                                    : FL_WIFI_AUTH_OPEN);
        s_synth_ap.band = cred->band_hint ? cred->band_hint : FL_WIFI_BAND_2GHZ;
        s_synth_ap.channel = 6;
        s_synth_ap.channel_width_mhz = 20;
        s_synth_ap.rssi_dbm = -70;
        lab_apply_ax_ap(&s_synth_ap);
        ap = &s_synth_ap;
    }
    if (ap->auth_mode != FL_WIFI_AUTH_OPEN && ap->auth_mode != FL_WIFI_AUTH_OWE &&
        cred->passphrase[0] == '\0')
        return FL_RESULT_INVAL;

    fl_net_loopback_mac_host(sta_mac);
    memset(pmk, 0, sizeof(pmk));
    rc = lab_supplicant_auth(cred, ap, sta_mac, pmk);
    if (rc != FL_RESULT_OK)
        rc = lab_derive_pmk(cred, pmk);
    if (rc != FL_RESULT_OK) {
        fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
        return rc;
    }

    rc = lab_run_mgmt_assoc(cred, ap, sta_mac, pmk);
    fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
    if (rc != FL_RESULT_OK)
        return rc;

    *ap_out = *ap;
    lab_fill_he(ap, he_out);
    s_negotiated_he = *he_out;
    s_lab_connected = 1;
    return FL_RESULT_OK;
}

fl_result_t wifi_lab_he_cap(fl_net_wifi_he_cap_t *cap_out)
{
    if (!cap_out)
        return FL_RESULT_INVAL;
    if (!s_lab_connected)
        return FL_RESULT_ERR;
    *cap_out = s_negotiated_he;
    return FL_RESULT_OK;
}

/* --- Lab FullMAC mock (FL_WIFI_80211AX_MOCK=1) --- */

#define MOCK_RX_SLOTS 4u
#define MOCK_RX_MAX   FL_NET_WIRE_FRAME_BUF_MAX

typedef struct {
	size_t len;
	uint8_t data[MOCK_RX_MAX];
} mock_rx_slot_t;

typedef struct {
	wifi_fullmac_t dev;
	fl_net_driver_t netdev;
	mock_rx_slot_t rx[MOCK_RX_SLOTS];
	unsigned rx_head;
	unsigned rx_count;
	int up;
	uint8_t sta_mac[6];
	uint8_t ap_bssid[6];
	char joined_ssid[FL_WIFI_SSID_MAX + 1u];
	wifi_auth_mode_t joined_auth;
	uint8_t joined_he;
	uint8_t joined_6ghz;
	uint8_t joined_bss_color;
	wifi_network_t scan[3];
	uint16_t scan_count;
	wifi_mgmt_transport_mock_ctx_t ota_tr_storage;
	fl_net_wifi_cred_t pending_cred;
	wifi_network_t pending_ap;
	int ota_auth_done;
	uint8_t ptk[64];
	size_t ptk_len;
	wifi_fullmac_twt_setup_t twt_slots[8];
	uint8_t twt_mask;
} wifi_lab_mock_ctx_t;

static wifi_lab_mock_ctx_t s_mock;

static const uint8_t s_ax_he_probe_ies[] = {
	FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
	0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
	0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x05
};

static void mock_seed_scan(wifi_lab_mock_ctx_t *ctx)
{
	memset(ctx->scan, 0, sizeof(ctx->scan));
	ctx->scan_count = 3;

	strncpy(ctx->scan[0].ssid, "MockAx6", sizeof(ctx->scan[0].ssid) - 1u);
	ctx->scan[0].bssid[0] = 0x02;
	ctx->scan[0].bssid[1] = 0xaa;
	ctx->scan[0].bssid[5] = 0x01;
	ctx->scan[0].rssi = -40;
	ctx->scan[0].channel = 37;
	ctx->scan[0].freq = 6135;
	ctx->scan[0].auth_mode = WIFI_AUTH_WPA3_SAE;

	strncpy(ctx->scan[1].ssid, "LegacyAC", sizeof(ctx->scan[1].ssid) - 1u);
	ctx->scan[1].bssid[0] = 0x02;
	ctx->scan[1].bssid[1] = 0xac;
	ctx->scan[1].bssid[5] = 0x02;
	ctx->scan[1].rssi = -55;
	ctx->scan[1].channel = 36;
	ctx->scan[1].freq = 5180;
	ctx->scan[1].auth_mode = WIFI_AUTH_WPA2_PSK;

	strncpy(ctx->scan[2].ssid, "MockOpen", sizeof(ctx->scan[2].ssid) - 1u);
	ctx->scan[2].bssid[0] = 0x02;
	ctx->scan[2].bssid[1] = 0x00;
	ctx->scan[2].bssid[5] = 0x03;
	ctx->scan[2].rssi = -60;
	ctx->scan[2].channel = 6;
	ctx->scan[2].freq = 2437;
	ctx->scan[2].auth_mode = WIFI_AUTH_OPEN;
}

static int mock_rx_enqueue(wifi_lab_mock_ctx_t *ctx, const uint8_t *frame, size_t len)
{
	unsigned idx;

	if (!ctx || !frame || len == 0 || len > MOCK_RX_MAX)
		return -1;
	if (ctx->rx_count >= MOCK_RX_SLOTS)
		return -1;
	idx = (ctx->rx_head + ctx->rx_count) % MOCK_RX_SLOTS;
	memcpy(ctx->rx[idx].data, frame, len);
	ctx->rx[idx].len = len;
	ctx->rx_count++;
	return 0;
}

static fl_result_t mock_build_ip_reply(const uint8_t *req_ip, size_t req_ip_len,
                                         const void *payload, size_t payload_len,
                                         uint8_t *out_ip, size_t out_cap)
{
	size_t hdr_len;
	uint16_t csum;

	if (!req_ip || req_ip_len < FL_NET_IPV4_HDR_LEN_MIN || !out_ip)
		return FL_RESULT_ERR;
	hdr_len = (size_t)((req_ip[0] & 0x0fu) * 4u);
	if (hdr_len + payload_len > out_cap)
		return FL_RESULT_ERR;
	memcpy(out_ip, req_ip, hdr_len);
	if (payload_len > 0u && payload)
		memcpy(out_ip + hdr_len, payload, payload_len);
	{
		uint8_t tmp[4];
		memcpy(tmp, out_ip + 12, 4);
		memcpy(out_ip + 12, out_ip + 16, 4);
		memcpy(out_ip + 16, tmp, 4);
	}
	{
		uint16_t total = (uint16_t)(hdr_len + payload_len);
		out_ip[2] = (uint8_t)(total >> 8);
		out_ip[3] = (uint8_t)(total & 0xff);
	}
	out_ip[10] = 0;
	out_ip[11] = 0;
	csum = fl_net_checksum16(out_ip, hdr_len);
	out_ip[10] = (uint8_t)(csum >> 8);
	out_ip[11] = (uint8_t)(csum & 0xff);
	return FL_RESULT_OK;
}

static fl_result_t mock_netdev_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame)
{
	wifi_lab_mock_ctx_t *ctx;
	size_t ip_off;
	size_t ip_len;
	uint8_t eth_reply[MOCK_RX_MAX];
	size_t eth_len = 0;

	if (!drv)
		return FL_RESULT_INVAL;
	ctx = (wifi_lab_mock_ctx_t *)((char *)drv -
				      offsetof(wifi_lab_mock_ctx_t, netdev));
	if (!ctx->up || !frame)
		return FL_RESULT_INVAL;
	if (fl_net_wire_check_view(frame, FL_NET_ETH_FRAME_HDR_LEN) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (wifi_driver_packet_validate_tx(frame->data, frame->len) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (!fl_net_wire_parse_eth_ipv4(frame->data, frame->len, &ip_off, &ip_len, NULL))
		return FL_RESULT_OK;

	{
		const uint8_t *ip = frame->data + ip_off;
		uint8_t ip_reply[MOCK_RX_MAX];
		size_t ip_reply_len = 0;

		if (ip_len >= FL_NET_IPV4_HDR_LEN_MIN && ip[9] == FL_NET_IP_PROTO_UDP) {
			size_t ihl = (size_t)((ip[0] & 0x0fu) * 4u);
			const uint8_t *udp = ip + ihl;
			uint16_t sport = (uint16_t)(((uint16_t)udp[0] << 8) | (uint16_t)udp[1]);
			uint16_t dport = (uint16_t)(((uint16_t)udp[2] << 8) | (uint16_t)udp[3]);
			uint16_t udp_total = (uint16_t)(((uint16_t)udp[4] << 8) | (uint16_t)udp[5]);
			size_t payload_len = (size_t)udp_total - FL_NET_UDP_HDR_LEN;
			uint32_t src_be = (uint32_t)ip[12] | ((uint32_t)ip[13] << 8) |
					  ((uint32_t)ip[14] << 16) | ((uint32_t)ip[15] << 24);
			uint32_t dst_be = (uint32_t)ip[16] | ((uint32_t)ip[17] << 8) |
					  ((uint32_t)ip[18] << 16) | ((uint32_t)ip[19] << 24);
			uint8_t udp_buf[FL_NET_UDP_HDR_LEN + 576];
			size_t udp_len;

			if (ihl + udp_total <= ip_len && payload_len > 0u) {
				udp_len = fl_net_udp_build_datagram(
					udp_buf, sizeof(udp_buf), dst_be, src_be, dport, sport,
					udp + FL_NET_UDP_HDR_LEN, payload_len);
				if (udp_len > 0u &&
				    mock_build_ip_reply(ip, ip_len, udp_buf, udp_len, ip_reply,
							sizeof(ip_reply)) == FL_RESULT_OK)
					ip_reply_len =
						(size_t)(((ip_reply[2] & 0xffu) << 8) |
							 (ip_reply[3] & 0xffu));
			}
		}
		if (ip_reply_len > 0u) {
			eth_len = fl_net_wire_build_eth_ipv4(eth_reply, sizeof(eth_reply),
							     ctx->sta_mac, ctx->ap_bssid,
							     ip_reply, ip_reply_len);
			if (eth_len > 0u)
				(void)mock_rx_enqueue(ctx, eth_reply, eth_len);
		}
	}
	return FL_RESULT_OK;
}

static fl_result_t mock_netdev_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out)
{
	wifi_lab_mock_ctx_t *ctx;
	fl_result_t rc;

	if (!drv || !out)
		return FL_RESULT_INVAL;
	ctx = (wifi_lab_mock_ctx_t *)((char *)drv -
				      offsetof(wifi_lab_mock_ctx_t, netdev));
	if (ctx->rx_count == 0)
		return FL_RESULT_TIMEDOUT;
	if (out->cap < ctx->rx[ctx->rx_head].len)
		return FL_RESULT_ERR;
	out->len = ctx->rx[ctx->rx_head].len;
	memcpy(out->data, ctx->rx[ctx->rx_head].data, out->len);
	ctx->rx_head = (ctx->rx_head + 1u) % MOCK_RX_SLOTS;
	ctx->rx_count--;
	rc = FL_RESULT_OK;
	(void)fl_net_wire_check_rx_fill(out, out->len);
	return rc;
}

static int mock_init(wifi_fullmac_t *dev)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;

	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_FW_READY;
	mock_seed_scan(ctx);
	return 0;
}

static int mock_deinit(wifi_fullmac_t *dev)
{
	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_DOWN;
	return 0;
}

static int mock_reset(wifi_fullmac_t *dev)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;

	if (!dev)
		return -1;
	ctx->up = 0;
	ctx->rx_head = 0;
	ctx->rx_count = 0;
	ctx->joined_ssid[0] = '\0';
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static int mock_start_scan(wifi_fullmac_t *dev, const char *ssid)
{
	(void)ssid;
	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_SCANNING;
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static int mock_get_scan_results(wifi_fullmac_t *dev, wifi_network_t *networks,
				 uint16_t *count)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;
	uint16_t n;
	uint16_t i;

	if (!dev || !networks || !count)
		return -1;
	n = ctx->scan_count;
	if (n > *count)
		n = *count;
	for (i = 0; i < n; i++)
		networks[i] = ctx->scan[i];
	*count = n;
	return 0;
}

static int mock_get_he_capabilities(wifi_fullmac_t *dev, wifi_fullmac_he_cap_t *he_cap)
{
	if (!dev || !he_cap)
		return -1;
	memset(he_cap, 0, sizeof(*he_cap));
	he_cap->ofdma_dl_supported = true;
	he_cap->ofdma_ul_supported = true;
	he_cap->mcs_nss[0] = 0x44;
	he_cap->mcs_nss[1] = 0x44;
	he_cap->max_ampdu_len_exp = 3;
	return 0;
}

static const wifi_network_t *mock_find_ssid(wifi_lab_mock_ctx_t *ctx, const char *ssid)
{
	uint16_t i;

	if (!ctx || !ssid)
		return NULL;
	for (i = 0; i < ctx->scan_count; i++) {
		if (!strcmp(ctx->scan[i].ssid, ssid))
			return &ctx->scan[i];
	}
	return NULL;
}

static const wifi_network_t *mock_find_bssid(wifi_lab_mock_ctx_t *ctx, const uint8_t bssid[6])
{
	uint16_t i;

	if (!ctx || !bssid)
		return NULL;
	for (i = 0; i < ctx->scan_count; i++) {
		if (!memcmp(ctx->scan[i].bssid, bssid, 6))
			return &ctx->scan[i];
	}
	return NULL;
}

static int mock_ota_set_key(void *opaque, const uint8_t *key, size_t key_len)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)opaque;

	if (!ctx || !key)
		return -1;
	if (key_len > sizeof(ctx->ptk))
		key_len = sizeof(ctx->ptk);
	memcpy(ctx->ptk, key, key_len);
	ctx->ptk_len = key_len;
	return 0;
}

static int mock_ota_init_transport(wifi_lab_mock_ctx_t *ctx, const wifi_network_t *ap,
				   wifi_mgmt_transport_t *tr_out)
{
	wifi_mgmt_transport_mock_cfg_t cfg = {
		.ap = ap,
		.sta_mac = ctx->sta_mac,
	};

	if (!ctx || !ap || !tr_out)
		return -1;
	return wifi_mgmt_transport_mock_init(tr_out, &ctx->ota_tr_storage, &cfg);
}

static int mock_run_supplicant_ota(wifi_lab_mock_ctx_t *ctx,
				    const fl_net_wifi_cred_t *cred,
				    const wifi_network_t *ap)
{
	wifi_mgmt_transport_t tr;
	wifi_connect_ota_hooks_t hooks = {
		.set_key = mock_ota_set_key,
		.ctx = ctx,
	};

	if (!ctx || !cred || !ap)
		return -1;
	if (mock_ota_init_transport(ctx, ap, &tr) != 0)
		return -1;
	return wifi_connect_ota_run(cred, ap, ctx->sta_mac, &tr, &hooks);
}

static int mock_setup_twt(wifi_fullmac_t *dev, const wifi_fullmac_twt_setup_t *twt)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;
	const wifi_network_t *ap;
	wifi_mgmt_transport_t tr;
	fl_net_wifi_twt_params_t req;
	fl_net_wifi_twt_params_t agreed;

	if (!dev || !twt || !ctx->up)
		return -1;
	ap = mock_find_bssid(ctx, ctx->ap_bssid);
	if (!ap)
		return -1;

	memset(&req, 0, sizeof(req));
	req.wake_duration_us = twt->wake_duration_us ? twt->wake_duration_us : 8000u;
	req.wake_interval_us = twt->wake_interval_ms ? (twt->wake_interval_ms * 1000u) : 100000u;
	if (mock_ota_init_transport(ctx, ap, &tr) != 0)
		return -1;
	if (wifi_twt_ota_setup(ctx->sta_mac, ctx->ap_bssid, &req, &agreed, &tr) != 0)
		return -1;

	ctx->twt_slots[agreed.flow_id] = *twt;
	ctx->twt_slots[agreed.flow_id].flow_id = agreed.flow_id;
	ctx->twt_slots[agreed.flow_id].wake_duration_us = agreed.wake_duration_us;
	ctx->twt_slots[agreed.flow_id].wake_interval_ms = agreed.wake_interval_us / 1000u;
	ctx->twt_mask |= (uint8_t)(1u << agreed.flow_id);
	fl_net_wifi_twt_power_schedule(&agreed);
	return 0;
}

static int mock_teardown_twt(wifi_fullmac_t *dev, uint8_t flow_id)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;
	const wifi_network_t *ap;
	wifi_mgmt_transport_t tr;

	if (!dev || flow_id >= 8u)
		return -1;
	if ((ctx->twt_mask & (1u << flow_id)) == 0u)
		return -1;
	ap = mock_find_bssid(ctx, ctx->ap_bssid);
	if (ap && mock_ota_init_transport(ctx, ap, &tr) == 0)
		(void)wifi_twt_ota_teardown(ctx->sta_mac, ctx->ap_bssid, flow_id, &tr);
	memset(&ctx->twt_slots[flow_id], 0, sizeof(ctx->twt_slots[flow_id]));
	ctx->twt_mask &= (uint8_t)~(1u << flow_id);
	return 0;
}

static int mock_authenticate(wifi_fullmac_t *dev, const uint8_t *bssid, uint16_t auth_type,
			     uint16_t auth_seq)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;
	const wifi_network_t *ap;
	wifi_mgmt_transport_t tr;
	wifi_connect_ota_hooks_t hooks = {
		.set_key = mock_ota_set_key,
		.ctx = ctx,
	};

	(void)auth_type;
	(void)auth_seq;
	if (!dev || !bssid || !ctx->pending_cred.ssid[0])
		return -1;
	ap = mock_find_bssid(ctx, bssid);
	if (!ap)
		return -1;
	ctx->pending_ap = *ap;
	if (mock_ota_init_transport(ctx, ap, &tr) != 0)
		return -1;
	if (wifi_connect_ota_run_phase(&ctx->pending_cred, ap, ctx->sta_mac, &tr, &hooks,
				       WIFI_CONNECT_OTA_AUTH_ONLY) != 0)
		return -1;
	ctx->ota_auth_done = 1;
	return 0;
}

static int mock_associate(wifi_fullmac_t *dev, const uint8_t *bssid)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;
	const wifi_network_t *ap;
	wifi_mgmt_transport_t tr;
	wifi_connect_ota_hooks_t hooks = {
		.set_key = mock_ota_set_key,
		.ctx = ctx,
	};

	if (!dev || !bssid || !ctx->pending_cred.ssid[0])
		return -1;
	ap = mock_find_bssid(ctx, bssid);
	if (!ap)
		return -1;
	if (mock_ota_init_transport(ctx, ap, &tr) != 0)
		return -1;
	if (!ctx->ota_auth_done &&
	    wifi_connect_ota_run_phase(&ctx->pending_cred, ap, ctx->sta_mac, &tr, &hooks,
				       WIFI_CONNECT_OTA_AUTH_ONLY) != 0)
		return -1;
	if (wifi_connect_ota_run_phase(&ctx->pending_cred, ap, ctx->sta_mac, &tr, &hooks,
				       WIFI_CONNECT_OTA_ASSOC_ONLY) != 0)
		return -1;
	memcpy(ctx->ap_bssid, ap->bssid, 6);
	strncpy(ctx->joined_ssid, ctx->pending_cred.ssid, sizeof(ctx->joined_ssid) - 1u);
	ctx->joined_auth = ap->auth_mode;
	ctx->joined_he = (ap->auth_mode == WIFI_AUTH_WPA3_SAE) ? 1u : 0u;
	ctx->joined_6ghz = (ap->auth_mode == WIFI_AUTH_WPA3_SAE) ? 1u : 0u;
	ctx->joined_bss_color = ctx->joined_he ? 5u : 0u;
	ctx->up = 1;
	ctx->rx_head = 0;
	ctx->rx_count = 0;
	ctx->ota_auth_done = 0;
	dev->state = WIFI_FULLMAC_STATE_CONNECTED;
	return 0;
}

static int mock_set_key(wifi_fullmac_t *dev, uint8_t key_index, const uint8_t *key,
			size_t key_len)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;

	(void)key_index;
	return mock_ota_set_key(ctx, key, key_len);
}

static int mock_tx_packet(wifi_fullmac_t *dev, const uint8_t *data, size_t len)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;
	fl_net_frame_view_t frame;

	if (!dev || !data)
		return -1;
	frame.data = data;
	frame.len = len;
	return mock_netdev_send(&ctx->netdev, &frame) == FL_RESULT_OK ? 0 : -1;
}

static int mock_rx_packet(wifi_fullmac_t *dev, uint8_t *buffer, size_t buf_len, size_t *out_len)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;
	fl_net_frame_mut_t out;

	if (!dev || !buffer || !out_len)
		return -1;
	out.data = buffer;
	out.cap = buf_len;
	out.len = 0;
	if (mock_netdev_recv(&ctx->netdev, &out) != FL_RESULT_OK)
		return -1;
	*out_len = out.len;
	return 0;
}

static int mock_deauthenticate(wifi_fullmac_t *dev, uint16_t reason)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;

	(void)reason;
	if (!dev)
		return -1;
	ctx->up = 0;
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static const wifi_fullmac_ops_t s_mock_ops = {
	.init = mock_init,
	.deinit = mock_deinit,
	.reset = mock_reset,
	.start_scan = mock_start_scan,
	.get_scan_results = mock_get_scan_results,
	.authenticate = mock_authenticate,
	.associate = mock_associate,
	.get_he_capabilities = mock_get_he_capabilities,
	.set_key = mock_set_key,
	.setup_twt = mock_setup_twt,
	.teardown_twt = mock_teardown_twt,
	.deauthenticate = mock_deauthenticate,
	.tx_packet = mock_tx_packet,
	.rx_packet = mock_rx_packet,
};

int wifi_lab_mock_attach(wifi_fullmac_t **out_dev)
{
	wifi_lab_mock_ctx_t *ctx = &s_mock;

	if (!out_dev)
		return -1;

	memset(ctx, 0, sizeof(*ctx));
	strncpy(ctx->dev.name, "mock_ax0", sizeof(ctx->dev.name) - 1u);
	ctx->dev.bus_type = WIFI_FULLMAC_BUS_PCIE;
	ctx->dev.state = WIFI_FULLMAC_STATE_DOWN;
	ctx->dev.vendor_id = 0x8086u;
	ctx->dev.device_id = 0x2725u;
	ctx->dev.ops = &s_mock_ops;
	ctx->dev.netdev = &ctx->netdev;
	ctx->netdev.send = mock_netdev_send;
	ctx->netdev.recv = mock_netdev_recv;
	ctx->netdev.mtu = FL_NET_ETH_MTU_DEFAULT;
	fl_net_loopback_mac_host(ctx->sta_mac);
	mock_seed_scan(ctx);
	if (mock_init(&ctx->dev) != 0)
		return -1;
	*out_dev = &ctx->dev;
	return 0;
}

void wifi_lab_mock_detach(wifi_fullmac_t *dev)
{
	if (!dev)
		return;
	(void)mock_deinit(dev);
	asm_mem_zero(&s_mock, sizeof(s_mock));
}

int wifi_lab_mock_connect(wifi_fullmac_t *dev, const fl_net_wifi_cred_t *cred)
{
	wifi_lab_mock_ctx_t *ctx = (wifi_lab_mock_ctx_t *)dev;
	const wifi_network_t *ap;

	if (!dev || !cred || !cred->ssid[0])
		return -1;

	ap = mock_find_ssid(ctx, cred->ssid);
	if (!ap)
		return -1;

	if (ap->auth_mode != WIFI_AUTH_OPEN && cred->passphrase[0] == '\0')
		return -1;

	memcpy(&ctx->pending_cred, cred, sizeof(ctx->pending_cred));
	ctx->ota_auth_done = 0;
	strncpy(ctx->joined_ssid, cred->ssid, sizeof(ctx->joined_ssid) - 1u);
	ctx->joined_auth = ap->auth_mode;
	ctx->joined_he = (ap->auth_mode == WIFI_AUTH_WPA3_SAE) ? 1u : 0u;
	ctx->joined_6ghz = (ap->auth_mode == WIFI_AUTH_WPA3_SAE) ? 1u : 0u;
	ctx->joined_bss_color = ctx->joined_he ? 5u : 0u;

	if (mock_run_supplicant_ota(ctx, cred, ap) != 0)
		return -1;

	memcpy(ctx->ap_bssid, ap->bssid, 6);
	ctx->up = 1;
	ctx->rx_head = 0;
	ctx->rx_count = 0;
	dev->state = WIFI_FULLMAC_STATE_CONNECTED;
	return 0;
}

void wifi_lab_mock_enrich_scan_entry(size_t index, fl_net_wifi_scan_entry_t *entry)
{
	if (!entry)
		return;
	if (index == 0u) {
		(void)fl_net_wifi_scan_enrich_from_ies(s_ax_he_probe_ies,
						       sizeof(s_ax_he_probe_ies), entry);
		entry->band = FL_WIFI_BAND_6GHZ;
		entry->channel = 37;
		entry->channel_width_mhz = 160;
		entry->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
	} else if (index == 1u) {
		entry->he_supported = 0;
		entry->band = FL_WIFI_BAND_5GHZ;
		entry->channel = 36;
		entry->channel_width_mhz = 80;
		entry->auth_mode = FL_WIFI_AUTH_WPA2_PSK;
	} else if (index == 2u) {
		entry->he_supported = 0;
		entry->band = FL_WIFI_BAND_2GHZ;
		entry->channel = 6;
		entry->channel_width_mhz = 20;
		entry->auth_mode = FL_WIFI_AUTH_OPEN;
	}
}

void wifi_lab_mock_fill_he_cap(fl_net_wifi_he_cap_t *cap)
{
	if (!cap)
		return;
	memset(cap, 0, sizeof(*cap));
	if (!s_mock.up)
		return;
	cap->max_nss_rx = 4;
	cap->max_nss_tx = 4;
	cap->supports_ofdma = s_mock.joined_he;
	cap->supports_mu_mimo = s_mock.joined_he;
	cap->supports_twt = s_mock.joined_he;
	cap->supports_6ghz = s_mock.joined_6ghz;
	cap->bss_color = s_mock.joined_bss_color;
	cap->channel_width_mhz = s_mock.joined_he ? 160u : 80u;
}
