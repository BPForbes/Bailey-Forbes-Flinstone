#include "net_dhcp.h"

#include "contract_p3_ipv4.h"
#include "net_ipv4.h"
#include "net_packet.h"
#include "net_route.h"
#include "net_udp.h"
#include "net_wire_host.h"

#include <string.h>

#define FL_NET_DHCP_BROADCAST_BE32 UINT32_C(0xffffffff)

static void dhcp_store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xffu);
    p[1] = (uint8_t)((v >> 16) & 0xffu);
    p[2] = (uint8_t)((v >> 8) & 0xffu);
    p[3] = (uint8_t)(v & 0xffu);
}

static uint32_t dhcp_load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

fl_result_t fl_net_dhcp_build_request(uint8_t *buf, size_t cap, uint8_t dhcp_msg_type, uint32_t xid,
                                      const uint8_t mac[FL_NET_ETH_ADDR_LEN], size_t *out_len) {
    size_t pos;

    if (!buf || !mac || !out_len || cap < FL_NET_BOOTP_FIXED_LEN + 8u)
        return FL_RESULT_INVAL;

    memset(buf, 0, FL_NET_BOOTP_FIXED_LEN);
    buf[0] = 1;
    buf[1] = 1;
    buf[2] = FL_NET_ETH_ADDR_LEN;
    dhcp_store_be32(buf + 4, xid);
    buf[10] = 0x80;
    memcpy(buf + 28, mac, FL_NET_ETH_ADDR_LEN);

    dhcp_store_be32(buf + 236, FL_NET_DHCP_MAGIC_COOKIE_BE32);
    pos = FL_NET_BOOTP_FIXED_LEN + 4u;

    if (pos + 4u > cap)
        return FL_RESULT_ERR;
    buf[pos++] = FL_NET_DHCP_OPT_MESSAGE_TYPE;
    buf[pos++] = 1u;
    buf[pos++] = dhcp_msg_type;
    buf[pos++] = FL_NET_DHCP_OPT_END;

    *out_len = pos;
    return FL_RESULT_OK;
}

fl_result_t fl_net_dhcp_build_request_pkt(fl_net_packet_t *pkt, uint8_t *backing, size_t cap,
                                          uint8_t dhcp_msg_type, uint32_t xid,
                                          const uint8_t mac[FL_NET_ETH_ADDR_LEN]) {
    size_t len = 0;
    fl_result_t rc;

    if (!pkt || !backing || !mac)
        return FL_RESULT_INVAL;

    rc = fl_net_dhcp_build_request(backing, cap, dhcp_msg_type, xid, mac, &len);
    if (rc != FL_RESULT_OK)
        return rc;
    return fl_net_packet_bind_l4(pkt, backing, cap, 0u, len);
}

static int dhcp_find_option(const uint8_t *opts, size_t opts_len, uint8_t code, uint8_t *val,
                            uint8_t val_cap) {
    size_t i = 0;

    while (i < opts_len) {
        uint8_t c = opts[i++];
        if (c == FL_NET_DHCP_OPT_PAD)
            continue;
        if (c == FL_NET_DHCP_OPT_END)
            break;
        if (i >= opts_len)
            break;
        {
            uint8_t len = opts[i++];
            if (i + len > opts_len)
                break;
            if (c == code) {
                uint8_t copy = len;
                if (copy > val_cap)
                    copy = val_cap;
                if (val && copy > 0u)
                    memcpy(val, opts + i, copy);
                return (int)copy;
            }
            i += len;
        }
    }
    return -1;
}

fl_result_t fl_net_dhcp_parse_reply(const uint8_t *buf, size_t len, uint32_t *xid_out,
                                    uint32_t *yiaddr_be_out, uint8_t *dhcp_msg_type_out) {
    const uint8_t *opts;
    size_t opts_len;
    uint8_t msg_type = 0;

    if (!buf || len < FL_NET_BOOTP_FIXED_LEN + 4u)
        return FL_RESULT_INVAL;
    if (buf[0] != 2)
        return FL_RESULT_ERR;

    if (xid_out)
        *xid_out = dhcp_load_be32(buf + 4);
    if (yiaddr_be_out)
        *yiaddr_be_out = dhcp_load_be32(buf + 16);

    opts = buf + FL_NET_BOOTP_FIXED_LEN;
    opts_len = len - FL_NET_BOOTP_FIXED_LEN;
    if (opts_len < 4u || dhcp_load_be32(opts) != FL_NET_DHCP_MAGIC_COOKIE_BE32)
        return FL_RESULT_ERR;

    opts += 4;
    opts_len -= 4;
    if (dhcp_find_option(opts, opts_len, FL_NET_DHCP_OPT_MESSAGE_TYPE, &msg_type, 1) < 1)
        return FL_RESULT_ERR;
    if (dhcp_msg_type_out)
        *dhcp_msg_type_out = msg_type;
    return FL_RESULT_OK;
}

fl_result_t fl_net_dhcp_parse_reply_pkt(const fl_net_packet_t *pkt, uint32_t *xid_out,
                                        uint32_t *yiaddr_be_out, uint8_t *dhcp_msg_type_out) {
    const uint8_t *buf;
    size_t len;
    fl_result_t rc;

    rc = fl_net_packet_l4_view(pkt, &buf, &len);
    if (rc != FL_RESULT_OK)
        return rc;
    return fl_net_dhcp_parse_reply(buf, len, xid_out, yiaddr_be_out, dhcp_msg_type_out);
}

static uint8_t dhcp_mask_be_to_prefix(uint32_t mask_be) {
    uint32_t m = ((uint32_t)mask_be >> 24) & 0xffu;
    m |= ((uint32_t)mask_be >> 8) & 0xff00u;
    m |= ((uint32_t)mask_be << 8) & 0xff0000u;
    m |= ((uint32_t)mask_be << 24) & 0xff000000u;
    uint8_t p = 0;
    while (p < 32u && (m & (UINT32_C(1) << (31u - p))))
        p++;
    return p;
}

fl_result_t fl_net_dhcp_parse_lease(const uint8_t *buf, size_t len,
                                    fl_net_dhcp_lease_info_t *lease_out) {
    const uint8_t *opts;
    size_t opts_len;
    uint8_t msg_type = 0;
    uint8_t mask_buf[4];
    uint8_t gw_buf[4];
    uint8_t dns_buf[4];

    if (!lease_out)
        return FL_RESULT_INVAL;
    memset(lease_out, 0, sizeof(*lease_out));

    if (fl_net_dhcp_parse_reply(buf, len, NULL, &lease_out->yiaddr_be, &msg_type) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (msg_type != FL_NET_DHCP_MSG_OFFER && msg_type != FL_NET_DHCP_MSG_ACK)
        return FL_RESULT_ERR;

    opts = buf + FL_NET_BOOTP_FIXED_LEN;
    opts_len = len - FL_NET_BOOTP_FIXED_LEN;
    if (opts_len < 4u || dhcp_load_be32(opts) != FL_NET_DHCP_MAGIC_COOKIE_BE32)
        return FL_RESULT_ERR;
    opts += 4;
    opts_len -= 4;

    if (dhcp_find_option(opts, opts_len, FL_NET_DHCP_OPT_SUBNET_MASK, mask_buf, 4) == 4) {
        lease_out->prefix_len = dhcp_mask_be_to_prefix(dhcp_load_be32(mask_buf));
        lease_out->has_prefix = 1;
    }
    if (dhcp_find_option(opts, opts_len, FL_NET_DHCP_OPT_ROUTER, gw_buf, 4) == 4) {
        lease_out->gateway_be = dhcp_load_be32(gw_buf);
        lease_out->has_gateway = 1;
    }
    if (dhcp_find_option(opts, opts_len, FL_NET_DHCP_OPT_DOMAIN_NAME_SERVER, dns_buf, 4) == 4) {
        lease_out->dns_be = dhcp_load_be32(dns_buf);
        lease_out->has_dns = 1;
    }
    return FL_RESULT_OK;
}

fl_result_t fl_net_dhcp_parse_lease_pkt(const fl_net_packet_t *pkt,
                                        fl_net_dhcp_lease_info_t *lease_out) {
    const uint8_t *buf;
    size_t len;
    fl_result_t rc;

    rc = fl_net_packet_l4_view(pkt, &buf, &len);
    if (rc != FL_RESULT_OK)
        return rc;
    return fl_net_dhcp_parse_lease(buf, len, lease_out);
}

static fl_result_t dhcp_exchange(fl_net_driver_t *drv, const uint8_t mac[FL_NET_ETH_ADDR_LEN],
                                 const fl_net_packet_t *req_pkt, fl_net_packet_t *reply_pkt,
                                 uint8_t *reply_backing, size_t reply_cap, size_t *reply_len,
                                 unsigned timeout_ms) {
    (void)drv;
    (void)mac;

    return fl_net_wire_send_udp_pkt(FL_NET_DHCP_BROADCAST_BE32, FL_NET_DHCP_CLIENT_PORT,
                                    FL_NET_DHCP_SERVER_PORT, req_pkt, reply_pkt, reply_backing,
                                    reply_cap, reply_len, timeout_ms);
}

fl_result_t fl_net_dhcp_acquire(fl_net_driver_t *drv, const uint8_t mac[FL_NET_ETH_ADDR_LEN],
                                const char *subnet_addr_s, unsigned prefix_len, const char *gw_s,
                                uint32_t *leased_addr_be, fl_net_dhcp_lease_info_t *lease_out,
                                unsigned timeout_ms) {
    uint8_t discover[300];
    uint8_t reply[576];
    size_t rlen = 0;
    uint32_t xid = 0x44584350u;
    uint32_t yiaddr = 0;
    uint8_t msg_type = 0;
    fl_net_dhcp_lease_info_t ack_lease;
    fl_net_packet_t discover_pkt;
    fl_net_packet_t reply_pkt;
    fl_result_t rc;
    char addr_buf[32];
    char gw_buf[32];

    if (!mac || !leased_addr_be)
        return FL_RESULT_INVAL;

    rc = fl_net_dhcp_build_request_pkt(&discover_pkt, discover, sizeof(discover),
                                       FL_NET_DHCP_MSG_DISCOVER, xid, mac);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = dhcp_exchange(drv, mac, &discover_pkt, &reply_pkt, reply, sizeof(reply), &rlen,
                       timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = fl_net_dhcp_parse_reply_pkt(&reply_pkt, NULL, &yiaddr, &msg_type);
    if (rc != FL_RESULT_OK || msg_type != FL_NET_DHCP_MSG_OFFER || yiaddr == 0u)
        return FL_RESULT_ERR;

    rc = fl_net_dhcp_build_request_pkt(&discover_pkt, discover, sizeof(discover),
                                       FL_NET_DHCP_MSG_REQUEST, xid, mac);
    if (rc != FL_RESULT_OK)
        return rc;

    rlen = 0;
    rc = dhcp_exchange(drv, mac, &discover_pkt, &reply_pkt, reply, sizeof(reply), &rlen,
                       timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    memset(&ack_lease, 0, sizeof(ack_lease));
    rc = fl_net_dhcp_parse_lease_pkt(&reply_pkt, &ack_lease);
    if (rc != FL_RESULT_OK || ack_lease.yiaddr_be == 0u)
        return FL_RESULT_ERR;
    rc = fl_net_dhcp_parse_reply_pkt(&reply_pkt, NULL, NULL, &msg_type);
    if (rc != FL_RESULT_OK || msg_type != FL_NET_DHCP_MSG_ACK)
        return FL_RESULT_ERR;

    *leased_addr_be = ack_lease.yiaddr_be;
    if (lease_out)
        *lease_out = ack_lease;

    if (drv) {
        uint8_t plen = ack_lease.has_prefix ? ack_lease.prefix_len : 24u;
        const char *gw_use = gw_s;

        fl_net_ipv4_format_addr(ack_lease.yiaddr_be, addr_buf, sizeof(addr_buf));
        if (!gw_use || !gw_use[0]) {
            if (ack_lease.has_gateway) {
                fl_net_ipv4_format_addr(ack_lease.gateway_be, gw_buf, sizeof(gw_buf));
                gw_use = gw_buf;
            }
        }
        if (subnet_addr_s && subnet_addr_s[0] && gw_use && gw_use[0] && prefix_len > 0u)
            plen = (uint8_t)prefix_len;
        if (gw_use && gw_use[0]) {
            rc = fl_net_route_configure_static(drv, mac, addr_buf, plen, gw_use);
            if (rc != FL_RESULT_OK)
                return rc;
        }
    }

    return FL_RESULT_OK;
}

fl_result_t fl_net_dhcp_lab_acquire(uint32_t *leased_addr_be, unsigned timeout_ms) {
    uint8_t mac[FL_NET_ETH_ADDR_LEN] = {0x02, 0x42, 0x00, 0x00, 0x01, 0x02};

    return fl_net_dhcp_acquire(NULL, mac, NULL, 0u, NULL, leased_addr_be, NULL, timeout_ms);
}
