/*
 * Lab virtual router — DHCP server with per-station IPv4 leases.
 * Driver-layer execution; kernel/core/net orchestrates the DHCP client only.
 */

#include "wifi_lab_router.h"

#include "net_ipv4.h"
#include "net_wire.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIFI_LAB_DHCP_LEASES 32u
#define WIFI_LAB_POOL_FIRST_HOST 16u
#define WIFI_LAB_POOL_LAST_HOST 254u

typedef struct wifi_lab_dhcp_lease {
    uint8_t mac[6];
    uint32_t yiaddr_be;
    unsigned in_use;
} wifi_lab_dhcp_lease_t;

static wifi_lab_dhcp_lease_t s_leases[WIFI_LAB_DHCP_LEASES];
static uint32_t s_net_be;
static uint8_t s_prefix_len;
static uint32_t s_gw_be;
static uint32_t s_dns_be;
static unsigned s_next_host;

static void wifi_lab_router_load_env(void)
{
    const char *pfx_s = getenv("FL_NET_WIFI_LAB_PREFIX");
    const char *gw_s = getenv("FL_NET_WIFI_LAB_GW");
    const char *dns_s = getenv("FL_NET_WIFI_LAB_DNS");
    const char *net_s = getenv("FL_NET_WIFI_LAB_NET");
    unsigned long pfx = 24u;

    if (!net_s || !net_s[0])
        net_s = "10.0.2.0";
    if (!gw_s || !gw_s[0])
        gw_s = "10.0.2.2";
    if (!dns_s || !dns_s[0])
        dns_s = "10.0.2.3";
    if (pfx_s && pfx_s[0]) {
        char *end = NULL;
        unsigned long v = strtoul(pfx_s, &end, 10);
        if (end != pfx_s && v > 0u && v <= 24u)
            pfx = v;
    }
    (void)fl_net_ipv4_parse_literal(net_s, &s_net_be);
    (void)fl_net_ipv4_parse_literal(gw_s, &s_gw_be);
    (void)fl_net_ipv4_parse_literal(dns_s, &s_dns_be);
    s_prefix_len = (uint8_t)pfx;
    if (s_net_be == 0u)
        (void)fl_net_ipv4_parse_literal("10.0.2.0", &s_net_be);
    if (s_gw_be == 0u)
        (void)fl_net_ipv4_parse_literal("10.0.2.2", &s_gw_be);
    if (s_dns_be == 0u)
        (void)fl_net_ipv4_parse_literal("10.0.2.3", &s_dns_be);
}

void wifi_lab_router_reset(void)
{
    memset(s_leases, 0, sizeof(s_leases));
    s_next_host = WIFI_LAB_POOL_FIRST_HOST;
    wifi_lab_router_load_env();
}

void wifi_lab_sta_mac(uint8_t mac[6])
{
    int pid;

    if (!mac)
        return;
    fl_net_loopback_mac_host(mac);
    pid = (int)getpid();
    mac[3] = (uint8_t)((pid >> 16) & 0xff);
    mac[4] = (uint8_t)((pid >> 8) & 0xff);
    mac[5] = (uint8_t)(pid & 0xff);
}

static void dhcp_store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xffu);
    p[1] = (uint8_t)((v >> 16) & 0xffu);
    p[2] = (uint8_t)((v >> 8) & 0xffu);
    p[3] = (uint8_t)(v & 0xffu);
}

static uint32_t dhcp_load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint32_t ipv4_prefix_to_mask_be(uint8_t prefix)
{
    if (prefix == 0u)
        return 0u;
    if (prefix >= 32u)
        return UINT32_C(0xffffffff);
    return (uint32_t)(UINT32_C(0xffffffff) << (32u - prefix));
}

static int dhcp_req_msg_type(const uint8_t *buf, size_t len, uint8_t *msg_out)
{
    const uint8_t *opts;
    size_t opts_len;
    size_t i;

    if (!buf || len < FL_NET_BOOTP_FIXED_LEN + 4u || !msg_out)
        return 0;
    if (buf[0] != 1u)
        return 0;
    opts = buf + FL_NET_BOOTP_FIXED_LEN;
    opts_len = len - FL_NET_BOOTP_FIXED_LEN;
    if (opts_len < 4u || dhcp_load_be32(opts) != FL_NET_DHCP_MAGIC_COOKIE_BE32)
        return 0;
    opts += 4;
    opts_len -= 4;
    for (i = 0; i < opts_len;) {
        uint8_t code = opts[i++];
        if (code == FL_NET_DHCP_OPT_PAD)
            continue;
        if (code == FL_NET_DHCP_OPT_END)
            break;
        if (i >= opts_len)
            break;
        {
            uint8_t olen = opts[i++];
            if (i + olen > opts_len)
                break;
            if (code == FL_NET_DHCP_OPT_MESSAGE_TYPE && olen >= 1u) {
                *msg_out = opts[i];
                return 1;
            }
            i += olen;
        }
    }
    return 0;
}

static int mac_equal(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, 6) == 0;
}

static wifi_lab_dhcp_lease_t *lease_find_mac(const uint8_t mac[6])
{
    unsigned i;

    for (i = 0; i < WIFI_LAB_DHCP_LEASES; i++) {
        if (s_leases[i].in_use && mac_equal(s_leases[i].mac, mac))
            return &s_leases[i];
    }
    return NULL;
}

static uint32_t lease_host_to_be(unsigned host_byte)
{
    uint32_t net = ntohl(s_net_be);
    uint32_t out;

    out = (net & 0xffffff00u) | (host_byte & 0xffu);
    return htonl(out);
}

static wifi_lab_dhcp_lease_t *lease_alloc(const uint8_t mac[6])
{
    wifi_lab_dhcp_lease_t *existing = lease_find_mac(mac);
    unsigned i;

    if (existing)
        return existing;

    for (i = 0; i < WIFI_LAB_DHCP_LEASES; i++) {
        if (!s_leases[i].in_use) {
            unsigned host = s_next_host;
            unsigned tries = 0;

            while (tries < (WIFI_LAB_POOL_LAST_HOST - WIFI_LAB_POOL_FIRST_HOST + 1u)) {
                wifi_lab_dhcp_lease_t *conflict = NULL;
                unsigned j;

                if (host < WIFI_LAB_POOL_FIRST_HOST || host > WIFI_LAB_POOL_LAST_HOST)
                    host = WIFI_LAB_POOL_FIRST_HOST;
                for (j = 0; j < WIFI_LAB_DHCP_LEASES; j++) {
                    if (s_leases[j].in_use &&
                        s_leases[j].yiaddr_be == lease_host_to_be(host)) {
                        conflict = &s_leases[j];
                        break;
                    }
                }
                if (!conflict)
                    break;
                host++;
                if (host > WIFI_LAB_POOL_LAST_HOST)
                    host = WIFI_LAB_POOL_FIRST_HOST;
                tries++;
            }

            memcpy(s_leases[i].mac, mac, 6);
            s_leases[i].yiaddr_be = lease_host_to_be(host);
            s_leases[i].in_use = 1;
            s_next_host = host + 1u;
            if (s_next_host > WIFI_LAB_POOL_LAST_HOST)
                s_next_host = WIFI_LAB_POOL_FIRST_HOST;
            return &s_leases[i];
        }
    }
    return NULL;
}

static size_t dhcp_append_opt_u32(uint8_t *out, size_t pos, size_t cap, uint8_t code, uint32_t v_be)
{
    if (pos + 6u > cap)
        return (size_t)-1;
    out[pos++] = code;
    out[pos++] = 4u;
    out[pos++] = (uint8_t)((v_be >> 24) & 0xffu);
    out[pos++] = (uint8_t)((v_be >> 16) & 0xffu);
    out[pos++] = (uint8_t)((v_be >> 8) & 0xffu);
    out[pos++] = (uint8_t)(v_be & 0xffu);
    return pos;
}

static fl_result_t dhcp_build_reply(uint8_t reply_msg, uint32_t xid, const uint8_t mac[6],
                                    uint32_t yiaddr_be, uint8_t *out, size_t cap, size_t *out_len)
{
    size_t pos;
    uint32_t mask_be;

    if (!mac || !out || !out_len || cap < FL_NET_BOOTP_FIXED_LEN + 24u)
        return FL_RESULT_INVAL;

    memset(out, 0, FL_NET_BOOTP_FIXED_LEN);
    out[0] = 2u;
    out[1] = 1u;
    out[2] = FL_NET_ETH_ADDR_LEN;
    dhcp_store_be32(out + 4, xid);
    dhcp_store_be32(out + 16, yiaddr_be);
    out[10] = 0x80u;
    memcpy(out + 28, mac, FL_NET_ETH_ADDR_LEN);

    dhcp_store_be32(out + 236, FL_NET_DHCP_MAGIC_COOKIE_BE32);
    pos = FL_NET_BOOTP_FIXED_LEN + 4u;
    if (pos + 4u > cap)
        return FL_RESULT_ERR;
    out[pos++] = FL_NET_DHCP_OPT_MESSAGE_TYPE;
    out[pos++] = 1u;
    out[pos++] = reply_msg;
    mask_be = ipv4_prefix_to_mask_be(s_prefix_len);
    pos = dhcp_append_opt_u32(out, pos, cap, FL_NET_DHCP_OPT_SUBNET_MASK, mask_be);
    if (pos == (size_t)-1)
        return FL_RESULT_ERR;
    pos = dhcp_append_opt_u32(out, pos, cap, FL_NET_DHCP_OPT_ROUTER, s_gw_be);
    if (pos == (size_t)-1)
        return FL_RESULT_ERR;
    pos = dhcp_append_opt_u32(out, pos, cap, FL_NET_DHCP_OPT_DOMAIN_NAME_SERVER, s_dns_be);
    if (pos == (size_t)-1)
        return FL_RESULT_ERR;
    if (pos + 1u > cap)
        return FL_RESULT_ERR;
    out[pos++] = FL_NET_DHCP_OPT_END;
    *out_len = pos;
    return FL_RESULT_OK;
}

fl_result_t wifi_lab_router_dhcp_exchange(const uint8_t cli_mac[6], const uint8_t *req,
                                          size_t req_len, uint8_t *reply, size_t reply_cap,
                                          size_t *reply_len)
{
    uint8_t req_msg = 0;
    uint32_t xid = 0;
    uint8_t reply_msg;
    wifi_lab_dhcp_lease_t *lease;

    if (!cli_mac || !req || !reply || !reply_len)
        return FL_RESULT_INVAL;

    if (s_net_be == 0u)
        wifi_lab_router_load_env();

    if (!dhcp_req_msg_type(req, req_len, &req_msg))
        return FL_RESULT_ERR;

    xid = dhcp_load_be32(req + 4);
    if (req_msg == FL_NET_DHCP_MSG_DISCOVER)
        reply_msg = FL_NET_DHCP_MSG_OFFER;
    else if (req_msg == FL_NET_DHCP_MSG_REQUEST)
        reply_msg = FL_NET_DHCP_MSG_ACK;
    else
        return FL_RESULT_ERR;

    lease = lease_alloc(cli_mac);
    if (!lease)
        return FL_RESULT_ERR;

    return dhcp_build_reply(reply_msg, xid, cli_mac, lease->yiaddr_be, reply, reply_cap,
                            reply_len);
}
