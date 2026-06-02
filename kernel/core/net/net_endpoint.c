#include "net_endpoint.h"

#include "contract_p3_server.h"
#include "net_endian.h"
#include "net_ipv4.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#define FL_NET_ENDPOINT_HOSTED 1
#include <arpa/inet.h>
#endif

static int parse_port(const char *port_str, uint16_t *port_out)
{
    long port;
    char *end = NULL;

    if (!port_str || !port_out)
        return 0;
    errno = 0;
    port = strtol(port_str, &end, 10);
    if (errno != 0 || end == port_str || (end && *end != '\0'))
        return 0;
    if (port <= 0 || port > 65535)
        return 0;
    *port_out = (uint16_t)port;
    return 1;
}

int fl_net_ipv6_wire_to_v4(const uint8_t addr_be[16], uint32_t *v4_be_out)
{
    static const uint8_t loop6[16] =
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const uint8_t mapped_prefix[12] =
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

    if (!addr_be || !v4_be_out)
        return 0;
    if (memcmp(addr_be, loop6, sizeof(loop6)) == 0) {
        static const uint8_t v4_loop_nbo[4] = {127u, 0u, 0u, 1u};
        *v4_be_out = fl_net_get_u32_nbo(v4_loop_nbo);
        return 1;
    }
    if (memcmp(addr_be, mapped_prefix, sizeof(mapped_prefix)) == 0) {
        *v4_be_out = fl_net_get_u32_nbo(addr_be + 12);
        return 1;
    }
    return 0;
}

int fl_net_v4_to_mapped_v6(uint32_t v4_be, uint8_t v6_be[16])
{
    if (!v6_be)
        return 0;
    memset(v6_be, 0, 16);
    v6_be[10] = 0xffu;
    v6_be[11] = 0xffu;
    v6_be[12] = (uint8_t)(v4_be & 0xffu);
    v6_be[13] = (uint8_t)((v4_be >> 8) & 0xffu);
    v6_be[14] = (uint8_t)((v4_be >> 16) & 0xffu);
    v6_be[15] = (uint8_t)((v4_be >> 24) & 0xffu);
    return 1;
}

int fl_net_endpoint_to_v4(const fl_net_endpoint_t *ep, uint32_t *v4_be_out)
{
    if (!ep || !v4_be_out)
        return 0;
    if (ep->family == FL_NET_ADDR_FAMILY_V4) {
        *v4_be_out = ep->addr.v4_be;
        return 1;
    }
    if (ep->family == FL_NET_ADDR_FAMILY_V6)
        return fl_net_ipv6_wire_to_v4(ep->addr.v6_be, v4_be_out);
    return 0;
}

void fl_net_endpoint_from_v4(uint32_t v4_be, uint16_t port_host, fl_net_endpoint_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->family = FL_NET_ADDR_FAMILY_V4;
    out->port_host = port_host;
    out->addr.v4_be = v4_be;
}

void fl_net_endpoint_from_v6(const uint8_t v6_be[16], uint16_t port_host, fl_net_endpoint_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->family = FL_NET_ADDR_FAMILY_V6;
    out->port_host = port_host;
    if (v6_be)
        memcpy(out->addr.v6_be, v6_be, 16);
}

int fl_net_endpoint_is_loopback(const fl_net_endpoint_t *ep)
{
    uint32_t v4 = 0u;
    if (!ep)
        return 0;
    if (ep->family == FL_NET_ADDR_FAMILY_V4)
        return fl_net_ipv4_is_loopback(ep->addr.v4_be);
    if (fl_net_endpoint_to_v4(ep, &v4))
        return fl_net_ipv4_is_loopback(v4);
    return 0;
}

int fl_net_endpoint_parse(const char *s, fl_net_endpoint_t *out)
{
    char buf[128];
    char host[128];
    size_t n;
    const char *port_str;

    if (!s || !out)
        return 0;
    n = strnlen(s, sizeof(buf));
    if (n == 0u || n >= sizeof(buf))
        return 0;
    memcpy(buf, s, n);
    buf[n] = '\0';

    if (buf[0] == '[') {
        char *end_br = strchr(buf, ']');
        size_t host_len;
        if (!end_br || end_br[1] != ':')
            return 0;
        host_len = (size_t)(end_br - (buf + 1));
        if (host_len == 0u || host_len >= sizeof(host))
            return 0;
        memcpy(host, buf + 1, host_len);
        host[host_len] = '\0';
        port_str = end_br + 2;
        if (fl_net_ipv4_parse_literal(host, &out->addr.v4_be)) {
            if (!parse_port(port_str, &out->port_host))
                return 0;
            out->family = FL_NET_ADDR_FAMILY_V4;
            return 1;
        }
#if defined(FL_NET_ENDPOINT_HOSTED)
        if (inet_pton(AF_INET6, host, out->addr.v6_be) != 1)
            return 0;
#else
        (void)host;
        return 0;
#endif
        if (!parse_port(port_str, &out->port_host))
            return 0;
        out->family = FL_NET_ADDR_FAMILY_V6;
        return 1;
    }

    {
        char *colon = strrchr(buf, ':');
        if (!colon || colon == buf)
            return 0;
        *colon = '\0';
        port_str = colon + 1;
        if (!fl_net_ipv4_parse_literal(buf, &out->addr.v4_be))
            return 0;
        if (!parse_port(port_str, &out->port_host))
            return 0;
        out->family = FL_NET_ADDR_FAMILY_V4;
        return 1;
    }
}

int fl_net_endpoint_parse_v4(const char *s, uint32_t *addr_be_out, uint16_t *port_out)
{
    fl_net_endpoint_t ep;
    if (!fl_net_endpoint_parse(s, &ep))
        return 0;
    if (!fl_net_endpoint_to_v4(&ep, addr_be_out))
        return 0;
    if (port_out)
        *port_out = ep.port_host;
    return 1;
}

_Static_assert(sizeof(fl_net_endpoint_t) == sizeof(fl_net_server_addr_t),
               "fl_net_endpoint_t and fl_net_server_addr_t must match");
_Static_assert(offsetof(fl_net_endpoint_t, family) == offsetof(fl_net_server_addr_t, family),
               "endpoint/server_addr family offset");
_Static_assert(offsetof(fl_net_endpoint_t, port_host) == offsetof(fl_net_server_addr_t, port_host),
               "endpoint/server_addr port_host offset");
_Static_assert(offsetof(fl_net_endpoint_t, addr) == offsetof(fl_net_server_addr_t, addr),
               "endpoint/server_addr address union offset");
