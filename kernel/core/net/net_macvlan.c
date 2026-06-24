/*
 * net_macvlan.c — macvlan LAN interface creation and standalone DHCP client.
 *
 * Creates fl0 as a macvlan child of the best detected LAN parent, then runs
 * a minimal AF_PACKET DHCP DISCOVER/REQUEST exchange so the real router
 * issues a unique IP distinct from the host OS address.  No pre-configured
 * route is required; AF_PACKET bypasses the IP layer for bootstrap.
 *
 * Linux-only.  All exported symbols compile to FL_RESULT_NOSYS stubs on
 * other platforms.
 */

#include "net_macvlan.h"
#include "net_checksum.h"

#if defined(__linux__)

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* ── DHCP constants ─────────────────────────────────────────────────── */

#define DHCP_BOOTREQUEST    1u
#define DHCP_BOOTREPLY      2u
#define DHCP_MSG_DISCOVER   1u
#define DHCP_MSG_OFFER      2u
#define DHCP_MSG_REQUEST    3u
#define DHCP_MSG_ACK        5u

#define DHCP_OPT_SUBNET    1u
#define DHCP_OPT_ROUTER    3u
#define DHCP_OPT_HOSTNAME 12u
#define DHCP_OPT_REQIP    50u
#define DHCP_OPT_MSGTYPE  53u
#define DHCP_OPT_SERVERID 54u
#define DHCP_OPT_PARAMREQ 55u
#define DHCP_OPT_END     255u

#define DHCP_FIXED_LEN   236u          /* op..giaddr + chaddr + sname + file */
#define DHCP_MAGIC_LEN     4u
#define DHCP_OPT_OFF    (DHCP_FIXED_LEN + DHCP_MAGIC_LEN)
#define DHCP_PKT_MAX     512u

#define IP_HDR_LEN  20u
#define UDP_HDR_LEN  8u
#define FRAME_MAX   (IP_HDR_LEN + UDP_HDR_LEN + DHCP_PKT_MAX)

/* ── Lease cache ────────────────────────────────────────────────────── */

static int      s_leased;
static uint32_t s_ip_be;
static uint32_t s_mask_be;
static uint32_t s_gw_be;

/* ── Internal helpers ───────────────────────────────────────────────── */

static int macvlan_ifname_valid(const char *name)
{
    size_t i;
    size_t len;

    if (!name || !name[0])
        return 0;
    len = strlen(name);
    if (len >= FL_NET_MACVLAN_IFNAMSIZ)
        return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            return 0;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':')
            continue;
        return 0;
    }
    return 1;
}

static int macvlan_run_ip(char *const argv[])
{
    pid_t pid;
    int status;

    if (!argv || !argv[0])
        return -1;
    if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) != 0)
        return -1;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int macvlan_ip_link_set_up(const char *name)
{
    char *argv[] = {"ip", "link", "set", (char *)name, "up", NULL};
    return macvlan_run_ip(argv);
}

static int macvlan_ip_link_add(const char *name, const char *parent)
{
    char *argv[] = {"ip", "link", "add", (char *)name, "link", (char *)parent,
                    "type", "macvlan", "mode", "bridge", NULL};
    return macvlan_run_ip(argv);
}

static int macvlan_ip_link_del(const char *name)
{
    char *argv[] = {"ip", "link", "del", (char *)name, NULL};
    return macvlan_run_ip(argv);
}

static int macvlan_ip_addr_add(const char *addr_pfx, const char *ifname)
{
    char *argv[] = {"ip", "addr", "add", (char *)addr_pfx, "dev", (char *)ifname, NULL};
    return macvlan_run_ip(argv);
}

static int detect_parent(char *out, size_t cap)
{
    struct ifaddrs *list, *ifa;
    uint32_t ip, hi;

    if (getifaddrs(&list) != 0) return 0;
    for (ifa = list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP))       continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)    continue;
        if (strncmp(ifa->ifa_name, "fl", 2u) == 0) continue;
        ip = ntohl(((const struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr);
        hi = ip >> 16;
        if (hi == 0xC0A8u ||
            (ip >> 24) == 10u ||
            (hi >= 0xAC10u && hi <= 0xAC1Fu)) {
            strncpy(out, ifa->ifa_name, cap - 1u);
            out[cap - 1u] = '\0';
            freeifaddrs(list);
            return 1;
        }
    }
    freeifaddrs(list);
    return 0;
}

static size_t build_discover(uint8_t *buf, size_t cap,
                              uint32_t xid, const uint8_t mac[6])
{
    size_t i;
    if (cap < DHCP_PKT_MAX) return 0u;
    memset(buf, 0, DHCP_PKT_MAX);
    buf[0] = (uint8_t)DHCP_BOOTREQUEST;
    buf[1] = 1u;  buf[2] = 6u;
    buf[4]  = (uint8_t)(xid >> 24); buf[5]  = (uint8_t)(xid >> 16);
    buf[6]  = (uint8_t)(xid >>  8); buf[7]  = (uint8_t) xid;
    buf[10] = 0x80u;                 /* broadcast flag */
    memcpy(buf + 28, mac, 6u);
    buf[236] = 0x63u; buf[237] = 0x82u;
    buf[238] = 0x53u; buf[239] = 0x63u;
    i = DHCP_OPT_OFF;
    buf[i++] = (uint8_t)DHCP_OPT_MSGTYPE;  buf[i++] = 1u;
    buf[i++] = (uint8_t)DHCP_MSG_DISCOVER;
    buf[i++] = (uint8_t)DHCP_OPT_HOSTNAME; buf[i++] = 9u;
    memcpy(buf + i, "flinstone", 9u);       i += 9u;
    buf[i++] = (uint8_t)DHCP_OPT_PARAMREQ; buf[i++] = 3u;
    buf[i++] = (uint8_t)DHCP_OPT_SUBNET;
    buf[i++] = (uint8_t)DHCP_OPT_ROUTER;
    buf[i++] = 6u;                           /* DNS */
    buf[i++] = (uint8_t)DHCP_OPT_END;
    return i;
}

static size_t build_request(uint8_t *buf, size_t cap,
                             uint32_t xid, const uint8_t mac[6],
                             uint32_t req_ip, uint32_t server_id)
{
    size_t i;
    if (cap < DHCP_PKT_MAX) return 0u;
    memset(buf, 0, DHCP_PKT_MAX);
    buf[0] = (uint8_t)DHCP_BOOTREQUEST;
    buf[1] = 1u; buf[2] = 6u;
    buf[4]  = (uint8_t)(xid >> 24); buf[5]  = (uint8_t)(xid >> 16);
    buf[6]  = (uint8_t)(xid >>  8); buf[7]  = (uint8_t) xid;
    buf[10] = 0x80u;
    memcpy(buf + 28, mac, 6u);
    buf[236] = 0x63u; buf[237] = 0x82u;
    buf[238] = 0x53u; buf[239] = 0x63u;
    i = DHCP_OPT_OFF;
    buf[i++] = (uint8_t)DHCP_OPT_MSGTYPE;  buf[i++] = 1u;
    buf[i++] = (uint8_t)DHCP_MSG_REQUEST;
    buf[i++] = (uint8_t)DHCP_OPT_REQIP;    buf[i++] = 4u;
    memcpy(buf + i, &req_ip,    4u); i += 4u;
    buf[i++] = (uint8_t)DHCP_OPT_SERVERID; buf[i++] = 4u;
    memcpy(buf + i, &server_id, 4u); i += 4u;
    buf[i++] = (uint8_t)DHCP_OPT_HOSTNAME; buf[i++] = 9u;
    memcpy(buf + i, "flinstone", 9u);       i += 9u;
    buf[i++] = (uint8_t)DHCP_OPT_END;
    return i;
}

/* Wrap a DHCP payload in an IP+UDP header for AF_PACKET SOCK_DGRAM send.
 * Source 0.0.0.0 → dest 255.255.255.255, sport 68, dport 67. */
static size_t wrap_ip_udp(uint8_t *out, size_t cap,
                           const uint8_t *payload, size_t plen)
{
    uint16_t udp_len = (uint16_t)(UDP_HDR_LEN + plen);
    uint16_t ip_tot  = (uint16_t)(IP_HDR_LEN  + udp_len);
    uint8_t *ip  = out;
    uint8_t *udp = out + IP_HDR_LEN;
    uint8_t *dat = out + IP_HDR_LEN + UDP_HDR_LEN;

    if (cap < (size_t)ip_tot) return 0u;
    memset(out, 0, (size_t)ip_tot);

    ip[0] = 0x45u;
    ip[2] = (uint8_t)(ip_tot >> 8); ip[3] = (uint8_t)ip_tot;
    ip[8] = 64u;  ip[9] = 17u;             /* TTL=64, proto=UDP */
    memset(ip + 16, 0xFFu, 4u);            /* dst = 255.255.255.255 */
    {
        uint16_t ck = fl_net_ipv4_checksum(ip, IP_HDR_LEN);
        ip[10] = (uint8_t)(ck >> 8); ip[11] = (uint8_t)ck;
    }

    udp[0] = 0u; udp[1] = 68u;             /* sport = 68 */
    udp[2] = 0u; udp[3] = 67u;             /* dport = 67 */
    udp[4] = (uint8_t)(udp_len >> 8); udp[5] = (uint8_t)udp_len;
    /* UDP checksum optional for IPv4 — leave 0 */

    memcpy(dat, payload, plen);
    return (size_t)ip_tot;
}

/* Parse an IP+UDP+DHCP reply received via AF_PACKET SOCK_DGRAM.
 * Returns 1 when the packet matches xid/mac and has expected_msgtype. */
static int parse_reply(const uint8_t *frame, size_t len,
                        uint32_t xid, const uint8_t mac[6],
                        uint8_t expected_msgtype,
                        uint32_t *yiaddr, uint32_t *server_id,
                        uint32_t *mask, uint32_t *gw)
{
    const uint8_t *dhcp;
    size_t dhcp_len, j;
    uint8_t ip_hlen, msgtype = 0u;
    uint32_t sid = 0u, m = 0u, g = 0u, yip = 0u, rxid;

    if (len < IP_HDR_LEN + UDP_HDR_LEN + DHCP_OPT_OFF + 4u) return 0;
    ip_hlen = (uint8_t)((frame[0] & 0x0Fu) * 4u);
    if (frame[9] != 17u) return 0;                      /* not UDP   */
    if ((size_t)ip_hlen + UDP_HDR_LEN >= len) return 0;
    /* dport must be 68 */
    if (frame[ip_hlen + 2u] != 0u || frame[ip_hlen + 3u] != 68u) return 0;

    dhcp     = frame + ip_hlen + UDP_HDR_LEN;
    dhcp_len = len   - ip_hlen - UDP_HDR_LEN;

    if (dhcp_len < DHCP_OPT_OFF + 4u) return 0;
    if (dhcp[0] != (uint8_t)DHCP_BOOTREPLY) return 0;

    rxid = ((uint32_t)dhcp[4] << 24) | ((uint32_t)dhcp[5] << 16) |
           ((uint32_t)dhcp[6] <<  8) |  (uint32_t)dhcp[7];
    if (rxid != xid) return 0;
    if (memcmp(dhcp + 28, mac, 6u) != 0) return 0;
    if (dhcp[236] != 0x63u || dhcp[237] != 0x82u ||
        dhcp[238] != 0x53u || dhcp[239] != 0x63u) return 0;

    memcpy(&yip, dhcp + 16u, 4u);   /* yiaddr — network byte order */

    j = DHCP_OPT_OFF;
    while (j < dhcp_len && dhcp[j] != DHCP_OPT_END) {
        uint8_t opt = dhcp[j++], olen;
        if (opt == 0u) continue;
        if (j >= dhcp_len) break;
        olen = dhcp[j++];
        if (j + olen > dhcp_len) break;
        if (opt == DHCP_OPT_MSGTYPE  && olen == 1u) msgtype = dhcp[j];
        else if (opt == DHCP_OPT_SERVERID && olen == 4u) memcpy(&sid, dhcp + j, 4u);
        else if (opt == DHCP_OPT_SUBNET   && olen == 4u) memcpy(&m,   dhcp + j, 4u);
        else if (opt == DHCP_OPT_ROUTER   && olen >= 4u) memcpy(&g,   dhcp + j, 4u);
        j += olen;
    }
    if (msgtype != expected_msgtype) return 0;

    if (yiaddr)    *yiaddr    = yip;
    if (server_id) *server_id = sid;
    if (mask)      *mask      = m;
    if (gw)        *gw        = g;
    return 1;
}

/* ── Public API ─────────────────────────────────────────────────────── */

fl_result_t fl_net_macvlan_create(const char *name,
                                  char parent_out[FL_NET_MACVLAN_IFNAMSIZ])
{
    char parent[FL_NET_MACVLAN_IFNAMSIZ];
    int sock;
    struct ifreq ifr;

    if (!macvlan_ifname_valid(name))
        return FL_RESULT_INVAL;

    /* If interface already exists, just bring it up. */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name, IFNAMSIZ - 1u);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
            if (!(ifr.ifr_flags & IFF_UP)) {
                if (macvlan_ip_link_set_up(name) != 0) {
                    close(sock);
                    return FL_RESULT_ERR;
                }
            }
            close(sock);
            return FL_RESULT_OK;
        }
        close(sock);
    }

    if (!detect_parent(parent, sizeof(parent)))
        return FL_RESULT_NOSYS;
    if (!macvlan_ifname_valid(parent))
        return FL_RESULT_ERR;
    if (parent_out)
        strncpy(parent_out, parent, FL_NET_MACVLAN_IFNAMSIZ - 1u);

    if (macvlan_ip_link_add(name, parent) != 0)
        return FL_RESULT_ACCES;

    if (macvlan_ip_link_set_up(name) != 0) {
        fl_net_macvlan_destroy(name);
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

void fl_net_macvlan_destroy(const char *name)
{
    if (!macvlan_ifname_valid(name))
        return;
    (void)macvlan_ip_link_del(name);
    s_leased = 0;
}

fl_result_t fl_net_macvlan_hwaddr(const char *ifname, uint8_t mac_out[6])
{
    int sock;
    struct ifreq ifr;

    if (!macvlan_ifname_valid(ifname))
        return FL_RESULT_INVAL;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return FL_RESULT_ERR;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1u);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
        close(sock);
        return FL_RESULT_ERR;
    }
    memcpy(mac_out, ifr.ifr_hwaddr.sa_data, 6u);
    close(sock);
    return FL_RESULT_OK;
}

fl_result_t fl_net_macvlan_dhcp_lease(const char *ifname,
                                       const uint8_t mac_in[6],
                                       uint32_t *ip_be,
                                       uint32_t *mask_be,
                                       uint32_t *gw_be,
                                       unsigned timeout_ms)
{
    unsigned ifindex;
    int sock = -1;
    struct sockaddr_ll sll;
    uint8_t dhcp_pkt[DHCP_PKT_MAX];
    uint8_t frame[FRAME_MAX];
    uint8_t reply[FRAME_MAX];
    size_t dhcp_len, frame_len;
    ssize_t n;
    fd_set rfds;
    struct timeval tv;
    uint32_t xid;
    uint32_t offered_ip = 0u, server_id = 0u, offered_mask = 0u, offered_gw = 0u;
    char ip_str[INET_ADDRSTRLEN];
    char addr_pfx[INET_ADDRSTRLEN + 8];
    unsigned deadline_s;
    time_t start;

    if (!macvlan_ifname_valid(ifname))
        return FL_RESULT_INVAL;

    ifindex = if_nametoindex(ifname);
    if (!ifindex) return FL_RESULT_NOSYS;

    srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)ifname);
    xid = ((uint32_t)rand() << 16) ^ (uint32_t)rand();

    sock = socket(AF_PACKET, SOCK_DGRAM, (int)htons(ETH_P_IP));
    if (sock < 0) return FL_RESULT_ERR;

    memset(&sll, 0, sizeof(sll));
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_IP);
    sll.sll_ifindex  = (int)ifindex;
    if (bind(sock, (const struct sockaddr *)&sll, sizeof(sll)) < 0) {
        close(sock);
        return FL_RESULT_ERR;
    }

    /* Destination: broadcast MAC */
    memset(&sll, 0, sizeof(sll));
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_IP);
    sll.sll_ifindex  = (int)ifindex;
    sll.sll_hatype   = ARPHRD_ETHER;
    sll.sll_pkttype  = PACKET_BROADCAST;
    sll.sll_halen    = 6u;
    memset(sll.sll_addr, 0xFFu, 6u);

    dhcp_len  = build_discover(dhcp_pkt, sizeof(dhcp_pkt), xid, mac_in);
    frame_len = wrap_ip_udp(frame, sizeof(frame), dhcp_pkt, dhcp_len);
    if (!frame_len || sendto(sock, frame, frame_len, 0,
                             (const struct sockaddr *)&sll, sizeof(sll)) < 0) {
        close(sock);
        return FL_RESULT_ERR;
    }

    /* Poll for OFFER — loop to discard non-DHCP packets. */
    start      = time(NULL);
    deadline_s = (timeout_ms + 999u) / 1000u;
    for (;;) {
        time_t elapsed = time(NULL) - start;
        long remain = (long)deadline_s - (long)elapsed;
        if (remain <= 0) { close(sock); return FL_RESULT_TIMEDOUT; }
        FD_ZERO(&rfds); FD_SET(sock, &rfds);
        tv.tv_sec = remain; tv.tv_usec = 0;
        if (select(sock + 1, &rfds, NULL, NULL, &tv) <= 0)
            { close(sock); return FL_RESULT_TIMEDOUT; }
        n = recvfrom(sock, reply, sizeof(reply), 0, NULL, NULL);
        if (n > 0 && parse_reply(reply, (size_t)n, xid, mac_in,
                                  DHCP_MSG_OFFER, &offered_ip, &server_id,
                                  &offered_mask, &offered_gw))
            break;
    }

    /* Send REQUEST */
    dhcp_len  = build_request(dhcp_pkt, sizeof(dhcp_pkt), xid, mac_in,
                               offered_ip, server_id);
    frame_len = wrap_ip_udp(frame, sizeof(frame), dhcp_pkt, dhcp_len);
    if (!frame_len || sendto(sock, frame, frame_len, 0,
                             (const struct sockaddr *)&sll, sizeof(sll)) < 0) {
        close(sock);
        return FL_RESULT_ERR;
    }

    /* Wait for ACK */
    for (;;) {
        time_t elapsed = time(NULL) - start;
        long remain = (long)deadline_s - (long)elapsed;
        if (remain <= 0) { close(sock); return FL_RESULT_TIMEDOUT; }
        FD_ZERO(&rfds); FD_SET(sock, &rfds);
        tv.tv_sec = remain; tv.tv_usec = 0;
        if (select(sock + 1, &rfds, NULL, NULL, &tv) <= 0)
            { close(sock); return FL_RESULT_TIMEDOUT; }
        n = recvfrom(sock, reply, sizeof(reply), 0, NULL, NULL);
        if (n > 0 && parse_reply(reply, (size_t)n, xid, mac_in,
                                  DHCP_MSG_ACK, &offered_ip, NULL,
                                  &offered_mask, &offered_gw))
            break;
    }
    close(sock);

    /* Configure IP on the interface */
    {
        uint8_t prefix = 24u;
        if (offered_mask) {
            uint32_t m = ntohl(offered_mask);
            uint8_t  p = 0u;
            while (m & 0x80000000u) { p++; m <<= 1; }
            prefix = p;
        }
        inet_ntop(AF_INET, &offered_ip, ip_str, sizeof(ip_str));
        snprintf(addr_pfx, sizeof(addr_pfx), "%s/%u", ip_str, (unsigned)prefix);
        if (macvlan_ip_addr_add(addr_pfx, ifname) != 0)
            return FL_RESULT_ERR;
    }

    s_leased  = 1;
    s_ip_be   = offered_ip;
    s_mask_be = offered_mask;
    s_gw_be   = offered_gw;

    if (ip_be)   *ip_be   = s_ip_be;
    if (mask_be) *mask_be = s_mask_be;
    if (gw_be)   *gw_be   = s_gw_be;
    return FL_RESULT_OK;
}

int fl_net_macvlan_get_registered(uint32_t *ip_be,
                                   uint32_t *mask_be,
                                   uint8_t  *prefix_len)
{
    uint32_t m;
    uint8_t  p = 24u;

    if (!s_leased) return 0;
    if (ip_be)   *ip_be   = s_ip_be;
    if (mask_be) *mask_be = s_mask_be;
    if (prefix_len) {
        m = ntohl(s_mask_be);
        p = 0u;
        while (m & 0x80000000u) { p++; m <<= 1; }
        *prefix_len = p;
    }
    return 1;
}

#else /* !__linux__ */

fl_result_t fl_net_macvlan_create(const char *name,
                                  char parent_out[FL_NET_MACVLAN_IFNAMSIZ])
{
    (void)name; (void)parent_out;
    return FL_RESULT_NOSYS;
}

void fl_net_macvlan_destroy(const char *name) { (void)name; }

fl_result_t fl_net_macvlan_hwaddr(const char *ifname, uint8_t mac_out[6])
{
    (void)ifname; (void)mac_out;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_macvlan_dhcp_lease(const char *ifname,
                                       const uint8_t mac_in[6],
                                       uint32_t *ip_be,
                                       uint32_t *mask_be,
                                       uint32_t *gw_be,
                                       unsigned timeout_ms)
{
    (void)ifname; (void)mac_in; (void)ip_be;
    (void)mask_be; (void)gw_be; (void)timeout_ms;
    return FL_RESULT_NOSYS;
}

int fl_net_macvlan_get_registered(uint32_t *ip_be,
                                   uint32_t *mask_be,
                                   uint8_t  *prefix_len)
{
    (void)ip_be; (void)mask_be; (void)prefix_len;
    return 0;
}

#endif /* __linux__ */
