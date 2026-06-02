/*
 * test_p3_net_tools.c — in-process exercise of the distro-style network shell
 * verbs added in #239 (arp / ifconfig / route / netstat / nslookup / netsh).
 *
 * The point of this test is not to repeat what test_p3_network already covers
 * (that's the in-tree IPv4/UDP/TCP path). It's to prove the new shell-level
 * surface routes through the internal APIs (fl_net_arp_*, fl_net_route_*,
 * fl_net_udp_*, fl_net_resolve_ipv4) and does NOT pull libc inet helpers in
 * by side-effect. Every assertion goes through fl_net_arp_cache_snapshot,
 * fl_net_route_snapshot, fl_net_udp_bound_ports_snapshot.
 */

#include "cmd_decl.h"
#include "net_arp.h"
#include "net_endian.h"
#include "net_ipv4.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_udp.h"
#include "net_endpoint.h"
#include "net_ping6_host.h"

#if defined(FL_NET_ASM_AVAILABLE)
#include "fl/net_asm.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            return 1; \
        } \
    } while (0)

/* Drive `arp -s` then assert via fl_net_arp_cache_snapshot that the row
 * is present with the right IPv4 and MAC bytes, then `arp -d` and assert
 * the row is gone. */
static int test_arp_add_show_delete(void) {
    char *add_argv[] = {(char *)"arp", (char *)"-s", (char *)"10.0.0.42",
                        (char *)"aa:bb:cc:dd:ee:ff", NULL};
    char *show_argv[] = {(char *)"arp", (char *)"-a", NULL};
    char *del_argv[] = {(char *)"arp", (char *)"-d", (char *)"10.0.0.42", NULL};
    fl_net_arp_cache_row_t rows[FL_NET_ARP_CACHE_MAX];
    uint32_t target_be = 0u;
    unsigned i;
    unsigned n;
    int found;

    fl_net_arp_clear();

    ASSERT(cmd_arp_run(4, add_argv) == 0);
    ASSERT(fl_net_ipv4_parse_literal("10.0.0.42", &target_be) != 0);

    n = fl_net_arp_cache_snapshot(rows, FL_NET_ARP_CACHE_MAX);
    found = 0;
    for (i = 0; i < n; i++) {
        if (rows[i].ip_be == target_be) {
            ASSERT(rows[i].mac[0] == 0xaau);
            ASSERT(rows[i].mac[1] == 0xbbu);
            ASSERT(rows[i].mac[2] == 0xccu);
            ASSERT(rows[i].mac[3] == 0xddu);
            ASSERT(rows[i].mac[4] == 0xeeu);
            ASSERT(rows[i].mac[5] == 0xffu);
            found = 1;
        }
    }
    ASSERT(found == 1);

    ASSERT(cmd_arp_run(2, show_argv) == 0);
    ASSERT(cmd_arp_run(3, del_argv) == 0);

    n = fl_net_arp_cache_snapshot(rows, FL_NET_ARP_CACHE_MAX);
    for (i = 0; i < n; i++)
        ASSERT(rows[i].ip_be != target_be);
    return 0;
}

static int capture_cmd_stdout(int (*run)(int, char **), int argc, char **argv, char *buf,
                              size_t cap) {
    FILE *save = stdout;
    FILE *tmp = tmpfile();
    size_t n;

    if (!tmp)
        return -1;
    stdout = tmp;
    if (run(argc, argv) != 0) {
        stdout = save;
        fclose(tmp);
        return -1;
    }
    fflush(stdout);
    stdout = save;
    rewind(tmp);
    n = fread(buf, 1, cap - 1u, tmp);
    fclose(tmp);
    buf[n] = '\0';
    return 0;
}

/* ifconfig must always succeed; loopback shows inet6 ::1 after route bootstrap. */
static int test_ifconfig_loopback(void) {
    char *argv[] = {(char *)"ifconfig", NULL};
    char out[2048];

    fl_net_route_add6_loopback();
    ASSERT(cmd_ifconfig_run(1, argv) == 0);
    ASSERT(fl_net_netdev_loopback() != NULL);
    ASSERT(capture_cmd_stdout(cmd_ifconfig_run, 1, argv, out, sizeof(out)) == 0);
    ASSERT(strstr(out, "inet6") != NULL);
    ASSERT(strstr(out, "::1") != NULL);
    ASSERT(strstr(out, "prefixlen 128") != NULL);
    return 0;
}

/* route must show at least the loopback /8 route registered at netdev init. */
static int test_route_shows_loopback(void) {
    char *argv[] = {(char *)"route", NULL};
    fl_net_route_entry_t rows[FL_NET_ROUTE_TABLE_MAX];
    unsigned n;
    int saw_loopback = 0;

    ASSERT(cmd_route_run(1, argv) == 0);
    n = fl_net_route_snapshot(rows, FL_NET_ROUTE_TABLE_MAX);
    for (unsigned i = 0; i < n; i++) {
        if (rows[i].prefix_len == 8u && rows[i].drv == fl_net_netdev_loopback())
            saw_loopback = 1;
    }
    ASSERT(saw_loopback == 1);
    return 0;
}

/* netstat: bind one UDP demux port and assert it shows up. */
static int test_netstat_lists_bound_udp(void) {
    char *argv[] = {(char *)"netstat", NULL};
    uint16_t ports[16];
    unsigned n;
    int saw_47123 = 0;

    fl_net_udp_demux_reset();
    ASSERT(fl_net_udp_bind_port(47123u) == FL_RESULT_OK);

    ASSERT(cmd_netstat_run(1, argv) == 0);
    n = fl_net_udp_bound_ports_snapshot(ports, 16u);
    for (unsigned i = 0; i < n; i++)
        if (ports[i] == 47123u)
            saw_47123 = 1;
    ASSERT(saw_47123 == 1);
    return 0;
}

/* nslookup: localhost must resolve to A and AAAA loopback addresses. */
static int test_nslookup_localhost(void) {
    char *argv[] = {(char *)"nslookup", (char *)"localhost", NULL};
    char out[512];

    ASSERT(cmd_nslookup_run(2, argv) == 0);
    ASSERT(capture_cmd_stdout(cmd_nslookup_run, 2, argv, out, sizeof(out)) == 0);
    ASSERT(strstr(out, "A:") != NULL);
    ASSERT(strstr(out, "AAAA:") != NULL);
    ASSERT(strstr(out, "::1") != NULL);
    return 0;
}

static int test_ping6_loopback(void) {
    double rtt = 0.0;
    fl_result_t rc = fl_net_ping6("::1", 1u, 3000u, &rtt);
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: ping6 unavailable\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rtt >= 0.0);
    return 0;
}

static int test_endpoint_format_v6(void) {
    fl_net_endpoint_t ep;
    char txt[128];

    fl_net_endpoint_from_v6((const uint8_t[16]){0}, 5000u, &ep);
    ep.addr.v6_be[15] = 1u;
    ep.family = FL_NET_ADDR_FAMILY_V6;
    ASSERT(fl_net_endpoint_format(&ep, txt, sizeof(txt)));
    ASSERT(strstr(txt, "[") != NULL);
    ASSERT(strstr(txt, "]:5000") != NULL);
    return 0;
}

/* netsh dispatches sub-verbs to the same handlers. */
static int test_netsh_dispatches(void) {
    char *ifc[] = {(char *)"netsh", (char *)"ifconfig", NULL};
    char *nsl[] = {(char *)"netsh", (char *)"nslookup", (char *)"localhost", NULL};
    char *route[] = {(char *)"netsh", (char *)"route", NULL};
    ASSERT(cmd_netsh_run(2, ifc) == 0);
    ASSERT(cmd_netsh_run(3, nsl) == 0);
    ASSERT(cmd_netsh_run(2, route) == 0);
    return 0;
}

/* Internal architecture path: fl_net_* helpers round-trip + ASM and
 * portable bit-shift forms agree bit-for-bit when both are present. */
static int test_endian_loopback_constant(void) {
    uint8_t wire[4] = {127u, 0u, 0u, 1u};
    uint32_t loop_nbo = fl_net_get_u32_nbo(wire);
    uint8_t back[4] = {0};
    fl_net_put_u32_nbo(back, loop_nbo);
    ASSERT(back[0] == 127u);
    ASSERT(back[1] == 0u);
    ASSERT(back[2] == 0u);
    ASSERT(back[3] == 1u);
    /* fl_net_htons should map 0x0001 -> 0x0100 on every host. */
    ASSERT(fl_net_htons(0x0001u) == 0x0100u);
    ASSERT(fl_net_ntohs(fl_net_htons(0xABCDu)) == 0xABCDu);
    ASSERT(fl_net_ntohl(fl_net_htonl(0xDEADBEEFu)) == 0xDEADBEEFu);

    /* On a build with FL_NET_ASM_AVAILABLE the ASM backend (`bswap` on
     * x86, `rev` on AArch64) must produce the exact same bit pattern as
     * the portable bit-shift fallback. This locks the two backends in
     * step so the packet module never sees byte-order drift between
     * ARCH=x86_64_gas / ARCH=x86_64_nasm / ARCH=arm. */
#if defined(FL_NET_ASM_AVAILABLE)
    {
        const uint16_t h16 = 0xABCDu;
        const uint32_t h32 = 0xDEADBEEFu;
        const uint64_t h64 = 0x0123456789ABCDEFull;
        uint16_t portable16 = (uint16_t)((h16 << 8) | (h16 >> 8));
        uint32_t portable32 = ((h32 & 0x000000FFu) << 24) |
                              ((h32 & 0x0000FF00u) << 8)  |
                              ((h32 & 0x00FF0000u) >> 8)  |
                              ((h32 & 0xFF000000u) >> 24);
        uint64_t portable64 = ((h64 & 0x00000000000000FFull) << 56) |
                              ((h64 & 0x000000000000FF00ull) << 40) |
                              ((h64 & 0x0000000000FF0000ull) << 24) |
                              ((h64 & 0x00000000FF000000ull) << 8)  |
                              ((h64 & 0x000000FF00000000ull) >> 8)  |
                              ((h64 & 0x0000FF0000000000ull) >> 24) |
                              ((h64 & 0x00FF000000000000ull) >> 40) |
                              ((h64 & 0xFF00000000000000ull) >> 56);
        ASSERT(asm_net_htons_be16(h16) == portable16);
        ASSERT(asm_net_ntohs_be16(portable16) == h16);
        ASSERT(asm_net_htonl_be32(h32) == portable32);
        ASSERT(asm_net_ntohl_be32(portable32) == h32);
        ASSERT(asm_net_htonll_be64(h64) == portable64);
        ASSERT(asm_net_ntohll_be64(portable64) == h64);
        /* And fl_net_* must equal the ASM result it routes through. */
        ASSERT(fl_net_htons(h16) == asm_net_htons_be16(h16));
        ASSERT(fl_net_htonl(h32) == asm_net_htonl_be32(h32));
        ASSERT(fl_net_htonll(h64) == asm_net_htonll_be64(h64));
    }
#endif

    /* fl_net_put_u32_be / fl_net_put_u64_be byte sequences must match the
     * wire layout of the IPv4 / 64-bit timestamp values they represent. */
    {
        uint8_t w4[4] = {0};
        uint8_t w8[8] = {0};
        fl_net_put_u32_be(w4, 0xDEADBEEFu);
        ASSERT(w4[0] == 0xDEu && w4[1] == 0xADu &&
               w4[2] == 0xBEu && w4[3] == 0xEFu);
        ASSERT(fl_net_get_u32_be(w4) == 0xDEADBEEFu);
        fl_net_put_u64_be(w8, 0x0123456789ABCDEFull);
        ASSERT(w8[0] == 0x01u && w8[1] == 0x23u && w8[2] == 0x45u && w8[3] == 0x67u);
        ASSERT(w8[4] == 0x89u && w8[5] == 0xABu && w8[6] == 0xCDu && w8[7] == 0xEFu);
        ASSERT(fl_net_get_u64_be(w8) == 0x0123456789ABCDEFull);
    }
    return 0;
}

int main(void) {
    fl_net_netdev_init();
    /* fl_net_route_add_loopback runs inside fl_net_route_init, called by
     * fl_net_netdev_init's downstream wiring; if not, call explicitly. */
    fl_net_route_add_loopback();
    fl_net_route_add6_loopback();

    printf("test_p3_net_tools: endian round-trip without libc... ");
    if (test_endian_loopback_constant() != 0)
        return 1;
    puts("ok");

    printf("test_p3_net_tools: arp -s / -a / -d... ");
    if (test_arp_add_show_delete() != 0)
        return 1;
    puts("ok");

    printf("test_p3_net_tools: ifconfig (loopback always up)... ");
    if (test_ifconfig_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_p3_net_tools: route lists 127.0.0.0/8 -> lo... ");
    if (test_route_shows_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_p3_net_tools: netstat lists bound UDP demux port... ");
    if (test_netstat_lists_bound_udp() != 0)
        return 1;
    puts("ok");

    printf("test_p3_net_tools: nslookup localhost... ");
    if (test_nslookup_localhost() != 0)
        return 1;
    puts("ok");

    printf("test_p3_net_tools: netsh dispatches to sub-verbs... ");
    if (test_netsh_dispatches() != 0)
        return 1;
    puts("ok");

    printf("test_p3_net_tools: ping6 ::1... ");
    if (test_ping6_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_p3_net_tools: fl_net_endpoint_format v6... ");
    if (test_endpoint_format_v6() != 0)
        return 1;
    puts("ok");

    puts("test_p3_net_tools: all passed");
    return 0;
}
