/*
 * 802.11ax AP/station L2 handshake relay over P3 server session wire.
 * Orchestrates session I/O; WiFi L2 execution lives in wifi_ax_session_driver.
 */

#include "net_wifi_ax_server.h"

#include "wifi_ax_session_driver.h"

#include <string.h>

#define AX_AP_CFG_MAX 4u

typedef struct {
    fl_net_server_t *srv;
    fl_net_wifi_ax_ap_config_t cfg;
    int enabled;
} ax_ap_slot_t;

/*
 * AP slot table: no mutex — callers must follow the server threading contract.
 * fl_net_wifi_ax_ap_enable() / fl_net_wifi_ax_ap_disable() run on the thread that
 * owns server setup/teardown (before fl_server_bg_start_server, after it stops).
 * fl_net_wifi_ax_ap_dispatch() runs only from the server poll thread for that
 * same fl_net_server_t instance. Do not enable/disable concurrently with dispatch.
 */
static ax_ap_slot_t s_ax_ap[AX_AP_CFG_MAX];

static ax_ap_slot_t *ax_ap_for_server(fl_net_server_t *srv)
{
    size_t i;
    for (i = 0; i < AX_AP_CFG_MAX; i++) {
        if (s_ax_ap[i].enabled && s_ax_ap[i].srv == srv)
            return &s_ax_ap[i];
    }
    return NULL;
}

fl_result_t fl_net_wifi_ax_ap_enable(fl_net_server_t *srv,
                                     const fl_net_wifi_ax_ap_config_t *cfg)
{
    size_t i;
    size_t slot = AX_AP_CFG_MAX;

    if (!srv || !cfg)
        return FL_RESULT_INVAL;
    for (i = 0; i < AX_AP_CFG_MAX; i++) {
        if (s_ax_ap[i].enabled && s_ax_ap[i].srv == srv) {
            s_ax_ap[i].cfg = *cfg;
            return FL_RESULT_OK;
        }
        if (!s_ax_ap[i].enabled && slot == AX_AP_CFG_MAX)
            slot = i;
    }
    if (slot >= AX_AP_CFG_MAX)
        return FL_RESULT_ERR;
    s_ax_ap[slot].srv = srv;
    s_ax_ap[slot].cfg = *cfg;
    s_ax_ap[slot].enabled = 1;
    return FL_RESULT_OK;
}

void fl_net_wifi_ax_ap_disable(fl_net_server_t *srv)
{
    size_t i;
    for (i = 0; i < AX_AP_CFG_MAX; i++) {
        if (s_ax_ap[i].enabled && s_ax_ap[i].srv == srv) {
            memset(&s_ax_ap[i], 0, sizeof(s_ax_ap[i]));
            return;
        }
    }
}

static fl_result_t ax_send(fl_net_sock_handle_t peer, uint8_t opcode,
                           const uint8_t *payload, uint16_t plen)
{
    if (peer == FL_NET_SOCK_INVALID)
        return FL_RESULT_ERR;
    return fl_net_session_send_frame(peer, opcode, payload, plen);
}

typedef struct {
    fl_net_sock_handle_t peer;
} ax_session_ctx_t;

static fl_result_t ax_session_send(void *ctx, uint8_t opcode, const uint8_t *payload,
                                   uint16_t plen)
{
    ax_session_ctx_t *sctx = (ax_session_ctx_t *)ctx;

    if (!sctx)
        return FL_RESULT_INVAL;
    return ax_send(sctx->peer, opcode, payload, plen);
}

static fl_result_t ax_session_recv(void *ctx, uint8_t expect_opcode, uint8_t *payload,
                                   uint16_t cap, uint16_t *plen_out, unsigned timeout_ms)
{
    ax_session_ctx_t *sctx = (ax_session_ctx_t *)ctx;
    uint8_t opcode = 0;
    fl_result_t rc;
    unsigned spins = timeout_ms ? timeout_ms / 10u + 1u : 500u;

    if (!sctx || !payload || !plen_out || sctx->peer == FL_NET_SOCK_INVALID)
        return FL_RESULT_INVAL;
    while (spins-- > 0u) {
        rc = fl_net_session_recv_frame(sctx->peer, &opcode, payload, cap, plen_out, 10u);
        if (rc == FL_RESULT_OK && opcode == expect_opcode)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_OK && rc != FL_RESULT_TIMEDOUT)
            return rc;
    }
    return FL_RESULT_TIMEDOUT;
}

int fl_net_wifi_ax_ap_dispatch(fl_net_server_t *srv, fl_net_server_member_id_t from,
                               fl_net_sock_handle_t peer, uint8_t opcode,
                               const uint8_t *payload, uint16_t plen)
{
    ax_ap_slot_t *ap = ax_ap_for_server(srv);
    uint8_t reply[512];
    size_t reply_len = 0;
    uint8_t msg1[64];
    uint8_t msg3[64];
    uint16_t msg1_len = 0;
    uint16_t msg3_len = 0;

    (void)from;
    if (!ap || !fl_net_session_is_wifi_opcode(opcode))
        return 0;

    switch (opcode) {
    case FL_NET_SESSION_OP_WIFI_SAE_COMMIT:
        if (ap->cfg.auth_mode != FL_WIFI_AUTH_WPA3_SAE || plen == 0u)
            return 1;
        if (wifi_ax_ap_sae_confirm(reply, sizeof(reply), &reply_len) != FL_RESULT_OK)
            return 1;
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_SAE_CONFIRM, reply,
                      (uint16_t)reply_len);
        return 1;

    case FL_NET_SESSION_OP_WIFI_EAPOL:
        if (ap->cfg.auth_mode != FL_WIFI_AUTH_WPA2_PSK)
            return 1;
        if (wifi_ax_ap_eapol_responses(msg1, &msg1_len, msg3, &msg3_len) != FL_RESULT_OK)
            return 1;
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_EAPOL, msg1, msg1_len);
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_EAPOL, msg3, msg3_len);
        return 1;

    case FL_NET_SESSION_OP_WIFI_ASSOC_REQ:
        if (wifi_ax_ap_assoc_response(ap->cfg.bssid, payload, plen, reply, sizeof(reply),
                                      &reply_len) != FL_RESULT_OK)
            return 1;
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_ASSOC_RESP, reply,
                      (uint16_t)reply_len);
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_AUTH_DONE, (const uint8_t *)"ok", 2u);
        return 1;

    default:
        return 1;
    }
}

fl_result_t fl_net_wifi_ax_station_ota(fl_net_client_t *client,
                                       const fl_net_wifi_cred_t *cred,
                                       uint8_t auth_mode, const uint8_t sta_mac[6],
                                       const uint8_t ap_bssid[6],
                                       fl_net_wifi_he_cap_t *he_cap_out,
                                       unsigned timeout_ms)
{
    ax_session_ctx_t session_ctx;
    wifi_ax_session_io_t io;
    uint8_t assoc_resp[512];
    uint16_t assoc_resp_len = 0;
    fl_result_t rc;

    if (!client || client->peer_handle == FL_NET_SOCK_INVALID || !cred || !sta_mac ||
        !ap_bssid)
        return FL_RESULT_INVAL;

    session_ctx.peer = client->peer_handle;
    io.ctx = &session_ctx;
    io.send = ax_session_send;
    io.recv = ax_session_recv;

    rc = wifi_ax_station_session_auth(cred, auth_mode, sta_mac, ap_bssid, &io, timeout_ms,
                                      assoc_resp, sizeof(assoc_resp), &assoc_resp_len);
    if (rc != FL_RESULT_OK)
        return rc;

    if (he_cap_out)
        (void)wifi_ax_station_parse_he_cap(assoc_resp, assoc_resp_len, he_cap_out);
    return FL_RESULT_OK;
}
