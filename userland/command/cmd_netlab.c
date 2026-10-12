/*
 * cmd_netlab.c — `netlab`: a fully self-contained network-security teaching
 * lab for Flinstone.
 *
 * WHAT THIS IS
 * ------------
 * `netlab` reproduces the *concepts* from the CSCI C 437 "Extending MiTM via
 * Reverse Shells" project (ARP spoofing, man-in-the-middle, reverse-shell
 * mechanics, traceroute, traffic capture, and pcap analysis) entirely inside
 * the emulator, against a FABRICATED topology that lives only in this
 * process's memory.
 *
 * WHAT THIS IS NOT
 * ----------------
 * It opens no sockets, touches no real interface, and reads no other user's
 * traffic or devices. It cannot ARP-poison a real segment, cannot inject
 * input into anyone else's terminal, and cannot rewrite a real capture to
 * fake a MiTM. Those are live-attack capabilities and are deliberately out of
 * scope; `netlab` teaches how they work and — just as importantly — how to
 * detect and prevent them, which is the half of the source report that
 * actually generalizes.
 *
 * The only real I/O is files: `netlab pcap write` emits a standard libpcap
 * file built from the SIMULATED frame script (every such file is stamped as
 * simulated), and `netlab pcap analyze` parses a real pcap you already have
 * and reports on it, including flagging the duplicate-MAC signature of ARP
 * spoofing. Reading and explaining a capture is ordinary defensive forensics.
 *
 * All verbs route through cmd_netlab_run(); the simulation state is the static
 * tables below. No kernel net headers are pulled in on purpose: the module is
 * libc-only so it is obviously incapable of real-network behaviour and is
 * trivially testable on its own (see tests/test_netlab_sim.c).
 */

#include "cmd_decl.h"
#include "fl/session.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Simulated topology (fabricated; not read from any real interface).        */
/* ------------------------------------------------------------------------- */

typedef enum {
    ROLE_GATEWAY = 0, /* the pod's simulated router/AP                     */
    ROLE_STATION,     /* an ordinary participant                          */
    ROLE_OBSERVER     /* the vantage point a MiTM/IDS would occupy         */
} nl_role_t;

typedef struct {
    const char *name;
    uint32_t    ip_be;   /* IPv4 in host struct, stored big-endian         */
    uint8_t     mac[6];
    nl_role_t   role;
} nl_host_t;

typedef struct {
    int             id;
    const char     *name;
    const char     *ssid;    /* simulated AP SSID for the pod               */
    int             consent; /* every seat in the pod has agreed (lab-only) */
    const nl_host_t *hosts;
    size_t          host_count;
    const char     *ap_sec;  /* simulated AP key-mgmt, e.g. "WPA2-PSK (CCMP)" */
    const char     *ap_psk;  /* FABRICATED lab-only pre-shared key           */
} nl_pod_t;

#define NL_MK_MAC(a, b, c, d, e, f)                                            \
    { (uint8_t)(a), (uint8_t)(b), (uint8_t)(c), (uint8_t)(d), (uint8_t)(e),    \
      (uint8_t)(f) }

/* 192.168.56.0/24-style private RFC1918 lab block (host-order -> big-endian) */
#define NL_IP(a, b, c, d) ((uint32_t)(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d)))

static const nl_host_t POD1_HOSTS[] = {
    { "gateway", NL_IP(192, 168, 56, 1),  NL_MK_MAC(0x02,0,0,0,0x56,0x01), ROLE_GATEWAY },
    { "alice",   NL_IP(192, 168, 56, 50), NL_MK_MAC(0x02,0,0,0,0x56,0x32), ROLE_STATION },
    { "bob",     NL_IP(192, 168, 56, 60), NL_MK_MAC(0x02,0,0,0,0x56,0x3c), ROLE_STATION },
    { "observer",NL_IP(192, 168, 56, 76), NL_MK_MAC(0x02,0,0,0,0x56,0x4c), ROLE_OBSERVER },
};

static const nl_host_t POD2_HOSTS[] = {
    { "gateway", NL_IP(10, 10, 0, 1),  NL_MK_MAC(0x02,0,0,0,0x0a,0x01), ROLE_GATEWAY },
    { "carol",   NL_IP(10, 10, 0, 20), NL_MK_MAC(0x02,0,0,0,0x0a,0x14), ROLE_STATION },
    { "ids",     NL_IP(10, 10, 0, 9),  NL_MK_MAC(0x02,0,0,0,0x0a,0x09), ROLE_OBSERVER },
};

static const nl_pod_t PODS[] = {
    { 1, "redteam-lab",  "FLINT-LAB-AP",  1, POD1_HOSTS, sizeof(POD1_HOSTS)/sizeof(POD1_HOSTS[0]),
      "WPA2-PSK (CCMP)", "flint-lab-RED-9t4w-demo" },
    { 2, "blueteam-lab", "FLINT-BLUE-AP", 1, POD2_HOSTS, sizeof(POD2_HOSTS)/sizeof(POD2_HOSTS[0]),
      "WPA3-SAE", "flint-lab-BLUE-2k8p-demo" },
};
static const size_t POD_COUNT = sizeof(PODS) / sizeof(PODS[0]);

/* ------------------------------------------------------------------------- */
/* Small formatting / lookup helpers (libc-only, no net stack).              */
/* ------------------------------------------------------------------------- */

static void fmt_ip(uint32_t be, char out[16]) {
    snprintf(out, 16, "%u.%u.%u.%u",
             (unsigned)((be >> 24) & 0xffu), (unsigned)((be >> 16) & 0xffu),
             (unsigned)((be >> 8) & 0xffu), (unsigned)(be & 0xffu));
}

static void fmt_mac(const uint8_t m[6], char out[18]) {
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static const char *role_str(nl_role_t r) {
    switch (r) {
    case ROLE_GATEWAY:  return "gateway";
    case ROLE_STATION:  return "station";
    case ROLE_OBSERVER: return "observer";
    default:            return "?";
    }
}

static const nl_pod_t *find_pod(const char *arg) {
    if (!arg || !arg[0])
        return NULL;
    for (size_t i = 0; i < POD_COUNT; i++) {
        char idbuf[16];
        snprintf(idbuf, sizeof(idbuf), "%d", PODS[i].id);
        if (!strcmp(arg, PODS[i].name) || !strcmp(arg, idbuf))
            return &PODS[i];
    }
    return NULL;
}

static const nl_host_t *find_host(const nl_pod_t *p, const char *name) {
    if (!p || !name)
        return NULL;
    for (size_t i = 0; i < p->host_count; i++)
        if (!strcmp(name, p->hosts[i].name))
            return &p->hosts[i];
    return NULL;
}

static const nl_host_t *pod_role(const nl_pod_t *p, nl_role_t r) {
    for (size_t i = 0; i < p->host_count; i++)
        if (p->hosts[i].role == r)
            return &p->hosts[i];
    return NULL;
}

static void print_boundary(void) {
    fputs("  [simulation] Fabricated topology in memory only. No real network,\n"
          "  [simulation] no interception of anyone else, no terminal injection.\n",
          stdout);
}

/* ------------------------------------------------------------------------- */
/* Simulated frame script (used by monitor / pcap write / arp-demo).         */
/*                                                                           */
/* The script tells the classic ARP-spoof story on fabricated hosts:        */
/*   1. station asks for the gateway's MAC (broadcast request)               */
/*   2. gateway answers truthfully (reply)                                   */
/*   3. observer gratuitously claims the gateway's IP  <-- the spoof         */
/*   4. observer also tells the gateway it owns the station's IP             */
/*   5. one UDP datagram station->gateway rides over the poisoned path       */
/* A capture of this is exactly what an analyst learns to recognize.        */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint8_t  bytes[64];
    uint16_t len;
    uint32_t ts_usec_off; /* microseconds after an arbitrary fixed base     */
    const char *note;
} nl_frame_t;

static const uint8_t BCAST[6]  = { 0xff,0xff,0xff,0xff,0xff,0xff };
static const uint8_t ZEROMAC[6]= { 0,0,0,0,0,0 };

/* Build one Ethernet+ARP frame. Returns byte length (42). */
static uint16_t build_arp(uint8_t *b, const uint8_t dmac[6], const uint8_t smac[6],
                          uint16_t oper, const uint8_t sha[6], uint32_t spa_be,
                          const uint8_t tha[6], uint32_t tpa_be) {
    size_t o = 0;
    memcpy(b + o, dmac, 6); o += 6;
    memcpy(b + o, smac, 6); o += 6;
    b[o++] = 0x08; b[o++] = 0x06;              /* ethertype ARP            */
    b[o++] = 0x00; b[o++] = 0x01;              /* htype ethernet           */
    b[o++] = 0x08; b[o++] = 0x00;              /* ptype IPv4               */
    b[o++] = 6;    b[o++] = 4;                 /* hlen / plen              */
    b[o++] = (uint8_t)(oper >> 8); b[o++] = (uint8_t)(oper & 0xff);
    memcpy(b + o, sha, 6); o += 6;
    b[o++] = (uint8_t)(spa_be >> 24); b[o++] = (uint8_t)(spa_be >> 16);
    b[o++] = (uint8_t)(spa_be >> 8);  b[o++] = (uint8_t)(spa_be & 0xff);
    memcpy(b + o, tha, 6); o += 6;
    b[o++] = (uint8_t)(tpa_be >> 24); b[o++] = (uint8_t)(tpa_be >> 16);
    b[o++] = (uint8_t)(tpa_be >> 8);  b[o++] = (uint8_t)(tpa_be & 0xff);
    return (uint16_t)o; /* 42 */
}

/* Build one Ethernet+IPv4+UDP frame with a short ASCII payload. */
static uint16_t build_udp(uint8_t *b, const uint8_t dmac[6], const uint8_t smac[6],
                          uint32_t src_be, uint32_t dst_be, const char *payload) {
    size_t o = 0, i;
    size_t plen = strlen(payload);
    uint16_t udp_len = (uint16_t)(8 + plen);
    uint16_t ip_len  = (uint16_t)(20 + udp_len);
    if (plen > 20) plen = 20, udp_len = (uint16_t)(8 + plen), ip_len = (uint16_t)(20 + udp_len);
    memcpy(b + o, dmac, 6); o += 6;
    memcpy(b + o, smac, 6); o += 6;
    b[o++] = 0x08; b[o++] = 0x00;              /* ethertype IPv4           */
    /* IPv4 header */
    b[o++] = 0x45; b[o++] = 0x00;              /* ver/ihl, dscp            */
    b[o++] = (uint8_t)(ip_len >> 8); b[o++] = (uint8_t)(ip_len & 0xff);
    b[o++] = 0x00; b[o++] = 0x01;              /* id                       */
    b[o++] = 0x00; b[o++] = 0x00;              /* flags/frag               */
    b[o++] = 0x40; b[o++] = 17;                /* ttl, proto UDP           */
    b[o++] = 0x00; b[o++] = 0x00;              /* checksum (left 0 in sim) */
    b[o++] = (uint8_t)(src_be >> 24); b[o++] = (uint8_t)(src_be >> 16);
    b[o++] = (uint8_t)(src_be >> 8);  b[o++] = (uint8_t)(src_be & 0xff);
    b[o++] = (uint8_t)(dst_be >> 24); b[o++] = (uint8_t)(dst_be >> 16);
    b[o++] = (uint8_t)(dst_be >> 8);  b[o++] = (uint8_t)(dst_be & 0xff);
    /* UDP header */
    b[o++] = 0xd4; b[o++] = 0x31;              /* src port 54321           */
    b[o++] = 0x1f; b[o++] = 0x90;              /* dst port 8080            */
    b[o++] = (uint8_t)(udp_len >> 8); b[o++] = (uint8_t)(udp_len & 0xff);
    b[o++] = 0x00; b[o++] = 0x00;              /* checksum (left 0 in sim) */
    for (i = 0; i < plen; i++)
        b[o++] = (uint8_t)payload[i];
    return (uint16_t)o;
}

/* Fill *out with the pod's scripted frames. Returns the count (<= cap). */
static size_t build_script(const nl_pod_t *p, nl_frame_t *out, size_t cap) {
    const nl_host_t *gw  = pod_role(p, ROLE_GATEWAY);
    const nl_host_t *obs = pod_role(p, ROLE_OBSERVER);
    const nl_host_t *st  = NULL;
    size_t n = 0;
    for (size_t i = 0; i < p->host_count; i++)
        if (p->hosts[i].role == ROLE_STATION) { st = &p->hosts[i]; break; }
    if (!gw || !obs || !st || cap < 5)
        return 0;

    out[n].len = build_arp(out[n].bytes, BCAST, st->mac, 1 /*request*/,
                           st->mac, st->ip_be, ZEROMAC, gw->ip_be);
    out[n].ts_usec_off = 0; out[n].note = "who-has gateway? (request, station->broadcast)"; n++;

    out[n].len = build_arp(out[n].bytes, st->mac, gw->mac, 2 /*reply*/,
                           gw->mac, gw->ip_be, st->mac, st->ip_be);
    out[n].ts_usec_off = 1200; out[n].note = "gateway is-at gateway_mac (truthful reply)"; n++;

    out[n].len = build_arp(out[n].bytes, BCAST, obs->mac, 2 /*reply*/,
                           obs->mac, gw->ip_be, BCAST, gw->ip_be);
    out[n].ts_usec_off = 2600; out[n].note = "SPOOF: observer claims gateway IP (gratuitous)"; n++;

    out[n].len = build_arp(out[n].bytes, gw->mac, obs->mac, 2 /*reply*/,
                           obs->mac, st->ip_be, gw->mac, gw->ip_be);
    out[n].ts_usec_off = 3100; out[n].note = "SPOOF: observer claims station IP to gateway"; n++;

    out[n].len = build_udp(out[n].bytes, gw->mac, st->mac, st->ip_be, gw->ip_be,
                           "GET /status");
    out[n].ts_usec_off = 5000; out[n].note = "UDP station->gateway over poisoned path"; n++;

    return n;
}

/* ------------------------------------------------------------------------- */
/* pcap writer (classic little-endian libpcap, LINKTYPE_ETHERNET).           */
/* ------------------------------------------------------------------------- */

static void put_u16le(uint8_t *b, uint16_t v) { b[0]=(uint8_t)(v&0xff); b[1]=(uint8_t)(v>>8); }
static void put_u32le(uint8_t *b, uint32_t v) {
    b[0]=(uint8_t)(v&0xff); b[1]=(uint8_t)((v>>8)&0xff);
    b[2]=(uint8_t)((v>>16)&0xff); b[3]=(uint8_t)((v>>24)&0xff);
}

#define PCAP_MAGIC_LE 0xa1b2c3d4u
#define PCAP_BASE_SEC 1704067200u /* fixed, obviously-synthetic base time    */

static int pcap_write(const nl_pod_t *p, const char *path) {
    nl_frame_t frames[16];
    size_t n = build_script(p, frames, sizeof(frames)/sizeof(frames[0]));
    uint8_t gh[24];
    FILE *f;
    if (n == 0) {
        fprintf(stderr, "netlab: could not build frame script for pod %s\n", p->name);
        return 1;
    }
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "netlab: cannot open '%s' for writing\n", path);
        return 1;
    }
    put_u32le(gh + 0, PCAP_MAGIC_LE);
    put_u16le(gh + 4, 2); put_u16le(gh + 6, 4);   /* version 2.4            */
    put_u32le(gh + 8, 0); put_u32le(gh + 12, 0);  /* thiszone, sigfigs      */
    put_u32le(gh + 16, 65535);                     /* snaplen                */
    put_u32le(gh + 20, 1);                          /* LINKTYPE_ETHERNET      */
    if (fwrite(gh, 1, sizeof(gh), f) != sizeof(gh)) { fclose(f); return 1; }

    for (size_t i = 0; i < n; i++) {
        uint8_t rh[16];
        put_u32le(rh + 0, PCAP_BASE_SEC);
        put_u32le(rh + 4, frames[i].ts_usec_off);
        put_u32le(rh + 8, frames[i].len);
        put_u32le(rh + 12, frames[i].len);
        if (fwrite(rh, 1, sizeof(rh), f) != sizeof(rh) ||
            fwrite(frames[i].bytes, 1, frames[i].len, f) != frames[i].len) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    printf("netlab: wrote %zu SIMULATED frames to %s (pod %s)\n", n, path, p->name);
    print_boundary();
    return 0;
}

/* ------------------------------------------------------------------------- */
/* pcap analyzer (reads a real file; defensive forensics).                   */
/* Handles LE/BE classic magic and the nanosecond variants; strict bounds.   */
/* ------------------------------------------------------------------------- */

static uint32_t rd_u32(const uint8_t *b, int le) {
    return le ? ((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24))
              : (((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3]);
}

#define NL_MAX_PKTS 100000u
#define NL_ARP_TRACK 256u

typedef struct { uint32_t ip_be; uint8_t mac[6]; } ip_mac_t;

static int pcap_analyze(const char *path) {
    FILE *f = fopen(path, "rb");
    uint8_t gh[24];
    uint32_t magic;
    int le = 1, ns = 0;
    uint32_t linktype;
    unsigned long pkts = 0, arp = 0, ipv4 = 0, other = 0;
    unsigned long conflicts = 0;
    ip_mac_t seen[NL_ARP_TRACK];
    size_t seen_n = 0;

    if (!f) {
        fprintf(stderr, "netlab: cannot open '%s' for reading\n", path);
        return 1;
    }
    if (fread(gh, 1, sizeof(gh), f) != sizeof(gh)) {
        fprintf(stderr, "netlab: '%s' is too short to be a pcap file\n", path);
        fclose(f);
        return 1;
    }
    magic = rd_u32(gh, 1);
    if (magic == 0xa1b2c3d4u)      { le = 1; ns = 0; }
    else if (magic == 0xd4c3b2a1u) { le = 0; ns = 0; }
    else if (magic == 0xa1b23c4du) { le = 1; ns = 1; }
    else if (magic == 0x4d3cb2a1u) { le = 0; ns = 1; }
    else {
        fprintf(stderr, "netlab: '%s' is not a classic pcap (bad magic 0x%08x)\n",
                path, magic);
        fclose(f);
        return 1;
    }
    linktype = rd_u32(gh + 20, le);
    printf("pcap: %s\n", path);
    printf("  byte order: %s-endian, timestamps: %s\n",
           le ? "little" : "big", ns ? "nanosecond" : "microsecond");
    if (linktype != 1) {
        printf("  link-layer type %u is not Ethernet; frame decode skipped.\n", linktype);
    }

    for (;;) {
        uint8_t rh[16];
        uint32_t incl, orig;
        uint8_t buf[65535];
        size_t got;
        if (fread(rh, 1, sizeof(rh), f) != sizeof(rh))
            break; /* clean EOF or trailing short record */
        incl = rd_u32(rh + 8, le);
        orig = rd_u32(rh + 12, le);
        (void)orig;
        if (incl > sizeof(buf)) {
            printf("  record %lu: incl_len %u exceeds snap cap; stopping.\n",
                   pkts + 1, incl);
            break;
        }
        got = fread(buf, 1, incl, f);
        if (got != incl)
            break; /* truncated final record */
        pkts++;
        if (pkts > NL_MAX_PKTS) {
            printf("  reached %u-packet cap; stopping.\n", NL_MAX_PKTS);
            break;
        }
        if (linktype != 1 || incl < 14)
            { other++; continue; }
        uint16_t eth = (uint16_t)((buf[12] << 8) | buf[13]);
        if (eth == 0x0806u && incl >= 42) {
            arp++;
            /* ARP sender: oper at 20..21, SHA 22..27, SPA 28..31 */
            uint32_t spa = rd_u32(buf + 28, 0 /*wire BE*/);
            const uint8_t *sha = buf + 22;
            size_t j;
            int found = 0;
            for (j = 0; j < seen_n; j++) {
                if (seen[j].ip_be == spa) {
                    found = 1;
                    if (memcmp(seen[j].mac, sha, 6) != 0) {
                        char ipb[16], m1[18], m2[18];
                        fmt_ip(spa, ipb); fmt_mac(seen[j].mac, m1); fmt_mac(sha, m2);
                        printf("  [ALERT] ARP conflict: %s claimed by %s and %s "
                               "(ARP-spoofing signature)\n", ipb, m1, m2);
                        conflicts++;
                    }
                    break;
                }
            }
            if (!found && seen_n < NL_ARP_TRACK) {
                seen[seen_n].ip_be = spa;
                memcpy(seen[seen_n].mac, sha, 6);
                seen_n++;
            }
        } else if (eth == 0x0800u) {
            ipv4++;
        } else {
            other++;
        }
    }
    fclose(f);

    printf("  packets: %lu   arp: %lu   ipv4: %lu   other: %lu\n",
           pkts, arp, ipv4, other);
    if (conflicts > 0)
        printf("  verdict: %lu ARP conflict(s) — consistent with a MiTM / ARP-spoof capture.\n",
               conflicts);
    else if (arp > 0)
        printf("  verdict: no duplicate IP->MAC bindings seen; ARP looks consistent.\n");
    else
        printf("  verdict: no ARP in this capture; nothing to correlate.\n");
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Interactive / printing verbs                                              */
/* ------------------------------------------------------------------------- */

static void print_usage(void) {
    fputs(
"netlab — contained network-security teaching lab (simulation only)\n"
"\n"
"  netlab consent                 Show the lab-only boundary this tool enforces\n"
"  netlab pods                    List simulated pods (private, isolated groups)\n"
"  netlab hosts <pod>             List a pod's simulated hosts (SSID/IP/MAC/role)\n"
"  netlab scan <pod>              Host discovery (nmap -sn style): up hosts + MACs\n"
"  netlab wpa <pod>               Show the pod AP's simulated security + PSK\n"
"  netlab topology <pod>          ASCII map of a pod\n"
"  netlab traceroute <pod> <dst>  Simulated hop path to a host in the pod\n"
"  netlab monitor <pod> [-c N]    Replay the pod's simulated frame script\n"
"  netlab arp-demo <pod>          Walk through how ARP-spoof MiTM works (illustration)\n"
"  netlab rs-explain              Explain reverse-shell mechanics (no injection)\n"
"  netlab pcap write <pod> <file> Emit a real .pcap of the SIMULATED frames\n"
"  netlab pcap analyze <file>     Parse a real .pcap; flag ARP-spoof signatures\n"
"  netlab defend                  Detection & prevention checklist\n"
"\n"
"Pods (membership keyed on your REAL logged-in account, lab networks by default):\n"
"  netlab pod create <pod>        Create a pod; you (the real account) are a member\n"
"  netlab pod join                Join the current pod as your real account\n"
"  netlab pod members | host      List members / show the pod host\n"
"  netlab pod leave               Leave the pod (host succession if the host leaves)\n"
"\n"
"  The pod host is a real member that is NOT also hosting the real `server`\n"
"  session, so one identity never relays both lab and real-server traffic. If\n"
"  every member is the real server host, the pod host stays \"pending\".\n"
"\n"
"Reverse shell (SIMULATED: listener = attacker, acceptor = victim):\n"
"  netlab listen                  Open a listener; you become the attacker\n"
"  netlab listen cancel           Withdraw your own open listener\n"
"  netlab accept                  Accept the open listener; you become the victim\n"
"  netlab <cmd>                   While bound, the ATTACKER's non-mgmt verbs run in\n"
"                                 a fresh, isolated sandbox (never the victim's real\n"
"                                 account): whoami, hostname, id, uname, pwd,\n"
"                                 ifconfig, netstat, ls, write, cat, rm\n"
"  netlab end                     Close the binding (either side may, any time)\n"
"  netlab status                  Show pod / host / reverse-shell state\n"
"\n"
"The victim side is always a disposable in-memory sandbox labeled with the\n"
"accepting account -- not that person's real session, files, or terminal. The\n"
"real `server` command is paused for the duration. Everything is fabricated in\n"
"memory — see `netlab consent`.\n",
        stdout);
}

static int verb_consent(void) {
    fputs(
"netlab operates on a FABRICATED topology inside this emulator only.\n"
"\n"
"It will NOT, and is not built to:\n"
"  - put any frame on a real network or read a real interface\n"
"  - intercept, relay, or alter another person's real traffic\n"
"  - send input into another person's terminal (no reverse shell into a victim)\n"
"  - rewrite a real capture to fabricate evidence of a MiTM\n"
"\n"
"It DOES teach the concepts and the defenses, and it does two kinds of real\n"
"file I/O: writing a pcap of its own simulated frames, and analyzing a pcap\n"
"you already have. Use the ideas here only on systems and networks you are\n"
"authorized to test, in an isolated lab, with everyone involved informed.\n",
        stdout);
    return 0;
}

static int verb_pods(void) {
    printf("Simulated pods (%zu):\n", POD_COUNT);
    for (size_t i = 0; i < POD_COUNT; i++) {
        printf("  pod %d  %-13s  ssid=%-14s  hosts=%zu  consent=%s\n",
               PODS[i].id, PODS[i].name, PODS[i].ssid, PODS[i].host_count,
               PODS[i].consent ? "all-agreed" : "NOT-SET");
    }
    printf("\nUse `netlab hosts <pod>` or `netlab topology <pod>`.\n");
    return 0;
}

static int verb_hosts(const nl_pod_t *p) {
    char ip[16], mac[18];
    printf("pod %d (%s)  ssid=%s\n", p->id, p->name, p->ssid);
    printf("  %-10s %-16s %-18s %s\n", "host", "ipv4", "mac", "role");
    for (size_t i = 0; i < p->host_count; i++) {
        fmt_ip(p->hosts[i].ip_be, ip);
        fmt_mac(p->hosts[i].mac, mac);
        printf("  %-10s %-16s %-18s %s\n", p->hosts[i].name, ip, mac,
               role_str(p->hosts[i].role));
    }
    return 0;
}

/* nmap -sn style host discovery: every simulated host is "up", with its MAC.
 * This is the reproduction report's `nmap -sn 192.168.x.0/24` step, which the
 * report uses to find MAC + local IP of devices — here on fabricated hosts. */
static int verb_scan(const nl_pod_t *p) {
    char ip[16], mac[18];
    uint32_t base = p->host_count ? (p->hosts[0].ip_be & 0xffffff00u) : 0;
    printf("netlab scan (simulated host discovery) pod %s — %u.%u.%u.0/24\n",
           p->name, (unsigned)((base >> 24) & 0xff), (unsigned)((base >> 16) & 0xff),
           (unsigned)((base >> 8) & 0xff));
    for (size_t i = 0; i < p->host_count; i++) {
        fmt_ip(p->hosts[i].ip_be, ip);
        fmt_mac(p->hosts[i].mac, mac);
        printf("  host up   %-16s MAC %-18s (%s %s)\n", ip, mac,
               role_str(p->hosts[i].role), p->hosts[i].name);
    }
    printf("  %zu hosts up (simulated). No real network was scanned.\n", p->host_count);
    return 0;
}

/* Reproduction report's "Get WPA Key" + "security settings for SSID" steps.
 * The PSK here is a FABRICATED lab value stored in the pod definition — netlab
 * reads no real NetworkManager config and recovers no real key. */
static int verb_wpa(const nl_pod_t *p) {
    printf("netlab wpa (simulated AP credentials) pod %s\n", p->name);
    printf("  ssid:     %s\n", p->ssid);
    printf("  key-mgmt: %s\n", p->ap_sec ? p->ap_sec : "(unknown)");
    printf("  psk:      %s\n", p->ap_psk ? p->ap_psk : "(none)");
    printf("  note: fabricated lab value. netlab does not read real saved\n");
    printf("        networks (/etc/NetworkManager/...) or crack any key.\n");
    printf("  defend: use a long random passphrase and WPA3-SAE; see `netlab defend`.\n");
    return 0;
}

static int verb_topology(const nl_pod_t *p) {
    const nl_host_t *gw = pod_role(p, ROLE_GATEWAY);
    char gwip[16];
    if (!gw) return 1;
    fmt_ip(gw->ip_be, gwip);
    printf("pod %d (%s), ssid \"%s\"\n\n", p->id, p->name, p->ssid);
    printf("        [ %s  %s ]  (gateway)\n", gw->name, gwip);
    printf("                 |\n");
    printf("        ---------+---------\n");
    for (size_t i = 0; i < p->host_count; i++) {
        char ip[16];
        if (p->hosts[i].role == ROLE_GATEWAY) continue;
        fmt_ip(p->hosts[i].ip_be, ip);
        printf("        |  %-9s %-16s %s\n", p->hosts[i].name, ip,
               p->hosts[i].role == ROLE_OBSERVER ? "(observer vantage)" : "");
    }
    return 0;
}

static int verb_traceroute(const nl_pod_t *p, const char *dst) {
    const nl_host_t *gw = pod_role(p, ROLE_GATEWAY);
    const nl_host_t *h  = find_host(p, dst);
    char ip[16];
    if (!h) {
        fprintf(stderr, "netlab: no host '%s' in pod %s\n", dst, p->name);
        return 1;
    }
    printf("traceroute (simulated) to %s in pod %s:\n", h->name, p->name);
    if (h->role == ROLE_GATEWAY) {
        fmt_ip(gw->ip_be, ip);
        printf("  1  %-9s %-16s  0.4 ms\n", gw->name, ip);
        return 0;
    }
    if (gw) {
        fmt_ip(gw->ip_be, ip);
        printf("  1  %-9s %-16s  0.4 ms\n", gw->name, ip);
    }
    fmt_ip(h->ip_be, ip);
    printf("  2  %-9s %-16s  0.9 ms\n", h->name, ip);
    return 0;
}

static int verb_monitor(const nl_pod_t *p, long limit) {
    nl_frame_t frames[16];
    size_t n = build_script(p, frames, sizeof(frames)/sizeof(frames[0]));
    if (n == 0) return 1;
    printf("monitor (simulated) pod %d (%s): replaying %zu frames\n", p->id, p->name, n);
    for (size_t i = 0; i < n; i++) {
        if (limit > 0 && (long)i >= limit) break;
        uint16_t eth = (uint16_t)((frames[i].bytes[12] << 8) | frames[i].bytes[13]);
        printf("  #%zu  t+%u us  %u B  %-4s  %s\n", i + 1, frames[i].ts_usec_off,
               frames[i].len, eth == 0x0806 ? "ARP" : eth == 0x0800 ? "IPv4" : "?",
               frames[i].note);
    }
    print_boundary();
    printf("Tip: `netlab pcap write %s out.pcap` then `netlab pcap analyze out.pcap`.\n",
           p->name);
    return 0;
}

static int verb_arp_demo(const nl_pod_t *p) {
    const nl_host_t *gw  = pod_role(p, ROLE_GATEWAY);
    const nl_host_t *obs = pod_role(p, ROLE_OBSERVER);
    const nl_host_t *st  = NULL;
    char gwip[16], gwmac[18], obsmac[18];
    for (size_t i = 0; i < p->host_count; i++)
        if (p->hosts[i].role == ROLE_STATION) { st = &p->hosts[i]; break; }
    if (!gw || !obs || !st) return 1;
    fmt_ip(gw->ip_be, gwip); fmt_mac(gw->mac, gwmac); fmt_mac(obs->mac, obsmac);

    printf("ARP-spoof MiTM, illustrated on pod %s (all simulated):\n\n", p->name);
    printf("1. Normally, when %s wants the gateway %s, it asks by broadcast and\n",
           st->name, gwip);
    printf("   the real gateway answers \"%s is-at %s\". %s caches that.\n\n",
           gwip, gwmac, st->name);
    printf("2. ARP has no authentication. The observer at %s sends its own\n", obsmac);
    printf("   unsolicited reply: \"%s is-at %s\". %s now caches the WRONG MAC.\n\n",
           gwip, obsmac, st->name);
    printf("3. With a matching lie sent to the gateway, both sides send through the\n");
    printf("   observer, which can read or alter the relayed traffic — the MiTM.\n\n");
    printf("Detection: two different MACs claiming %s is the giveaway. Run\n", gwip);
    printf("   `netlab pcap write %s demo.pcap && netlab pcap analyze demo.pcap`\n", p->name);
    printf("   to see the analyzer raise that exact alert.\n\n");
    printf("Prevention: static/\"sticky\" ARP for gateways, dynamic ARP inspection on\n");
    printf("   managed switches, and alerting on gratuitous-ARP floods. See `netlab defend`.\n");
    return 0;
}

static int verb_rs_explain(void) {
    fputs(
"Reverse shell — how it works, and why defenders watch for it:\n"
"\n"
"A reverse shell flips who dials whom. Instead of an operator connecting IN to\n"
"a machine (which inbound firewall rules usually block), the machine makes an\n"
"OUTBOUND connection to a listener and wires its shell to that socket. Outbound\n"
"is commonly allowed, so this slips past inbound filtering — that is the whole\n"
"point of the technique in the source report.\n"
"\n"
"Why netlab does not implement one: a working reverse shell means executing an\n"
"operator's commands on someone else's machine. That is remote control of a\n"
"victim, not a simulation, so it is out of scope here regardless of the network.\n"
"\n"
"What to watch for instead (defender's view):\n"
"  - unexpected OUTBOUND connections to odd ports (4444 is a classic default)\n"
"  - a shell process that is the parent of a network socket\n"
"  - egress rules: default-deny outbound, allow only what is needed\n"
"  - EDR/IDS signatures for known payload and listener patterns\n",
        stdout);
    return 0;
}

static int verb_defend(void) {
    fputs(
"Detection & prevention checklist (from the MiTM+RS material):\n"
"\n"
"ARP spoofing / MiTM:\n"
"  - Dynamic ARP Inspection (DAI) on managed switches; static ARP for gateways\n"
"  - Alert on gratuitous ARP and on any IP with two live MACs (what netlab flags)\n"
"  - Prefer encrypted + authenticated protocols so interception yields little\n"
"  - WPA2/WPA3 on Wi-Fi; segment and minimize broadcast domains\n"
"\n"
"Reverse shells / RATs:\n"
"  - Default-deny egress; allow only required outbound destinations and ports\n"
"  - Watch outbound to uncommon ports; correlate shells spawning sockets\n"
"  - EDR + IDS/IPS for payload, listener, and C2 beacon patterns\n"
"  - Key-based SSH, fail2ban, and least-privilege accounts\n"
"\n"
"DNS / general:\n"
"  - DNSSEC to resist DNS spoofing; keep systems patched\n"
"  - Capture and review traffic routinely; `netlab pcap analyze <file>` helps\n",
        stdout);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* Stateful session: pods (membership + host succession) and a SIMULATED      */
/* reverse shell. All state is in-memory for this one lab seat. The reverse   */
/* shell runs against a MOCK victim profile — `netlab <cmd>` returns the       */
/* victim's fabricated output. No real participant's session is controlled.   */
/* ------------------------------------------------------------------------- */

/*
 * netlab session state, this process. Pods are keyed on the REAL logged-in
 * account (fl_session_current_user()) rather than a free-typed name.
 *
 * Unlike the freestanding/browser shell, this hosted process has exactly ONE
 * current identity at a time (no concurrent per-terminal session table) --
 * fl_session_current_user() is process-global. A pod "member" here is
 * therefore whichever real account was logged in (via login/su/switchuser)
 * at the moment it ran `netlab pod join`; simulating a second real
 * participant means actually switching to their real account between turns,
 * the same way any other command in this shell proves identity. This is a
 * weaker concurrency guarantee than the freestanding shell's real per-session
 * table and is documented as such rather than overclaimed.
 *
 * The reverse shell is still a SIMULATION: accepting a listener binds the
 * attacker identity to a fresh, isolated, disposable sandbox labeled with
 * the accepting identity -- never that account's real shell, real files, or
 * real `server` session. The accepting party can end the binding at any
 * time, and the real `server` command refuses while a binding is open (see
 * cmd_netlab_is_isolated(), checked from cmd_server_run()).
 */
#define NL_NAMELEN 32
#define NL_MAXMEM  8

static char s_pod[NL_NAMELEN];
static char s_members[NL_MAXMEM][NL_NAMELEN]; /* real account names, join order */
static int  s_memcount;
static int  s_hostidx;                        /* -1 = pending (collision)       */

static char s_listen_name[NL_NAMELEN];  /* "" = no open listener           */
static char s_atk_name[NL_NAMELEN];     /* "" = not bound                  */
static char s_victim_name[NL_NAMELEN];
static int  s_isolated;                 /* gates cmd_server_run() while set */

/* Disposable per-binding sandbox: a tiny isolated file table. Reset on every
 * new accept() and on end(). Never backed by the real filesystem. */
#define NL_SB_FILES 8
#define NL_SB_CONTENTLEN 64
static char s_sb_name[NL_SB_FILES][NL_NAMELEN];
static char s_sb_content[NL_SB_FILES][NL_SB_CONTENTLEN];
static int  s_sb_used[NL_SB_FILES];

int cmd_netlab_is_isolated(void) { return s_isolated; }

static void nl_cpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    if (!n) return;
    for (; s[i] && i + 1 < n; i++) d[i] = s[i];
    d[i] = 0;
}

static int nl_member_idx(const char *name) {
    for (int i = 0; i < s_memcount; i++)
        if (!strcmp(s_members[i], name)) return i;
    return -1;
}

/* Reassign the pod host: first member (join order) that is not currently
 * hosting the real `server` session. Leaves s_hostidx at -1 ("pending") if
 * every member is that real host. */
static void nl_pick_host(void) {
    s_hostidx = -1;
    for (int i = 0; i < s_memcount; i++) {
        if (cmd_server_is_running() && !strcmp(s_members[i], fl_session_current_user()))
            continue;
        s_hostidx = i;
        break;
    }
}

static void nl_sb_reset(void) {
    for (int i = 0; i < NL_SB_FILES; i++) s_sb_used[i] = 0;
}

/* Server lifecycle hooks, called by cmd_server when THIS process was hosting
 * the real server. On leave the host steps down (the server picks a new host
 * by its own join order); we re-elect the pod host independently by pod join
 * order, so the two new hosts may differ. On kill the server is torn down, so
 * the pods that lived under it are killed too. */
void cmd_netlab_on_server_leave(void) {
    if (!s_pod[0]) return;
    nl_pick_host();
}

/* Called when THIS process starts hosting the real server. A pod host and the
 * server host are two distinct relay points, so a single identity cannot hold
 * both at once; re-elect the pod host immediately so becoming the server host
 * hands the pod host role to someone else (or leaves it pending). */
void cmd_netlab_on_server_host(void) {
    if (!s_pod[0]) return;
    nl_pick_host();
}

void cmd_netlab_on_server_kill(void) {
    if (!s_pod[0]) return;
    s_pod[0] = 0; s_memcount = 0; s_hostidx = -1;
    s_listen_name[0] = 0; s_atk_name[0] = 0; s_victim_name[0] = 0;
    nl_sb_reset();
    s_isolated = 0;
    printf("netlab: server killed by its host — pod dissolved.\n");
}

static int nl_sb_find(const char *name) {
    for (int i = 0; i < NL_SB_FILES; i++)
        if (s_sb_used[i] && !strcmp(s_sb_name[i], name)) return i;
    return -1;
}

/* Deterministic pseudo-octet so the sandbox's mock iface is stable per name. */
static unsigned nl_name_octet(const char *v) {
    unsigned h = 0;
    for (const char *p = v; *p; p++) h = (h * 31u + (unsigned char)*p) & 0xffu;
    return 50u + (h % 180u);
}

/* Real execution against the disposable sandbox's own isolated state -- not
 * canned text, but never the accepting account's real files either. */
static void nl_sb_run(int argc, char **argv) {
    const char *cmd = argv[1];
    const char *v = s_victim_name;
    unsigned o = nl_name_octet(v);
    if (!strcmp(cmd, "whoami"))        { printf("%s\n", v); return; }
    if (!strcmp(cmd, "hostname"))      { printf("%s-sandbox\n", v); return; }
    if (!strcmp(cmd, "id"))            { printf("uid=1000(%s) gid=1000(%s)\n", v, v); return; }
    if (!strcmp(cmd, "uname"))         { printf("FlinstoneLab x86_64 (isolated sandbox)\n"); return; }
    if (!strcmp(cmd, "pwd"))           { printf("/sandbox/%s\n", v); return; }
    if (!strcmp(cmd, "ifconfig"))      { printf("eth0  inet 192.168.56.%u  ether 02:00:00:00:56:%02x  (simulated)\n", o, o & 0xffu); return; }
    if (!strcmp(cmd, "netstat"))       { printf("tcp  192.168.56.%u:4444  ESTABLISHED  (simulated reverse-shell socket)\n", o); return; }
    if (!strcmp(cmd, "ls")) {
        int any = 0;
        for (int i = 0; i < NL_SB_FILES; i++)
            if (s_sb_used[i]) { printf("%s  ", s_sb_name[i]); any = 1; }
        printf(any ? "\n" : "(empty sandbox)\n");
        return;
    }
    if (!strcmp(cmd, "write")) {
        if (argc < 4) { fprintf(stderr, "usage: write <file> <content>\n"); return; }
        int idx = nl_sb_find(argv[2]);
        if (idx < 0) {
            int slot = 0;
            while (slot < NL_SB_FILES && s_sb_used[slot]) slot++;
            if (slot >= NL_SB_FILES) { fprintf(stderr, "sandbox full\n"); return; }
            nl_cpy(s_sb_name[slot], argv[2], NL_NAMELEN);
            s_sb_used[slot] = 1;
            idx = slot;
        }
        s_sb_content[idx][0] = 0;
        for (int i = 3; i < argc; i++) {
            if (i > 3) strncat(s_sb_content[idx], " ", NL_SB_CONTENTLEN - 1 - strlen(s_sb_content[idx]));
            strncat(s_sb_content[idx], argv[i], NL_SB_CONTENTLEN - 1 - strlen(s_sb_content[idx]));
        }
        printf("wrote %s (sandbox)\n", argv[2]);
        return;
    }
    if (!strcmp(cmd, "cat")) {
        if (argc < 3) { fprintf(stderr, "usage: cat <file>\n"); return; }
        int idx = nl_sb_find(argv[2]);
        if (idx < 0) { fprintf(stderr, "cat: %s: not found\n", argv[2]); return; }
        printf("%s\n", s_sb_content[idx]);
        return;
    }
    if (!strcmp(cmd, "rm")) {
        if (argc < 3) { fprintf(stderr, "usage: rm <file>\n"); return; }
        int idx = nl_sb_find(argv[2]);
        if (idx < 0) { fprintf(stderr, "rm: %s: not found\n", argv[2]); return; }
        s_sb_used[idx] = 0;
        printf("removed %s (sandbox)\n", argv[2]);
        return;
    }
    printf("%s: ran in the isolated sandbox (output not modeled)\n", cmd);
}

static int nl_is_mgmt_verb(const char *v) {
    static const char *m[] = {
        "help","-h","--help","consent","pods","rs-explain","defend","hosts",
        "topology","arp-demo","monitor","scan","wpa","traceroute","pcap",
        "pod","listen","accept","end","status", NULL
    };
    for (int i = 0; m[i]; i++) if (!strcmp(v, m[i])) return 1;
    return 0;
}

static int verb_pod(int argc, char **argv) {
    const char *sub = (argc >= 3) ? argv[2] : "";
    fl_session_init();
    if (!strcmp(sub, "create")) {
        const char *me = fl_session_current_user();
        if (argc < 4) { fprintf(stderr, "usage: netlab pod create <pod>\n"); return 1; }
        nl_cpy(s_pod, argv[3], NL_NAMELEN);
        s_memcount = 0;
        nl_cpy(s_members[s_memcount++], me, NL_NAMELEN);
        nl_pick_host();
        printf("netlab: pod '%s' created.\n", s_pod);
        if (s_hostidx >= 0) {
            printf("  host is %s (simulated, lab networks by default)\n", s_members[s_hostidx]);
        } else {
            printf("  host pending: you are currently the real server host, so this pod\n");
            printf("  has no pod host yet. Have another real account join, or `server kill`\n");
            printf("  your own real hosting, to free up a pod host.\n");
        }
        return 0;
    }
    if (!strcmp(sub, "join")) {
        const char *me = fl_session_current_user();
        if (!s_pod[0]) { fprintf(stderr, "netlab: no pod yet. `netlab pod create <pod>` first.\n"); return 1; }
        if (nl_member_idx(me) >= 0) { printf("netlab: %s is already in pod %s.\n", me, s_pod); return 0; }
        if (s_memcount >= NL_MAXMEM) { fprintf(stderr, "netlab: pod '%s' is full.\n", s_pod); return 1; }
        nl_cpy(s_members[s_memcount++], me, NL_NAMELEN);
        nl_pick_host();
        printf("netlab: %s joined pod %s (identity preserved).\n", me, s_pod);
        if (s_hostidx >= 0) printf("  host: %s\n", s_members[s_hostidx]);
        else printf("  host pending: every current member is the real server host.\n");
        return 0;
    }
    if (!strcmp(sub, "members")) {
        if (!s_pod[0]) { printf("netlab: no pod. `netlab pod create <pod>`.\n"); return 0; }
        printf("pod %s members (%d):\n", s_pod, s_memcount);
        for (int i = 0; i < s_memcount; i++) {
            printf("  %s%s%s\n", s_members[i], i == s_hostidx ? "  (host)" : "",
                   (cmd_server_is_running() && !strcmp(s_members[i], fl_session_current_user()))
                       ? "  (real server host)" : "");
        }
        return 0;
    }
    if (!strcmp(sub, "host")) {
        if (!s_pod[0]) { printf("netlab: no pod.\n"); return 0; }
        printf("pod %s host: %s\n", s_pod, s_hostidx >= 0 ? s_members[s_hostidx] : "(none - pending)");
        return 0;
    }
    if (!strcmp(sub, "leave")) {
        const char *me = fl_session_current_user();
        int idx, was_host;
        if (!s_pod[0]) { fprintf(stderr, "netlab: not in a pod.\n"); return 1; }
        idx = nl_member_idx(me);
        if (idx < 0) { fprintf(stderr, "netlab: you are not in this pod.\n"); return 1; }
        was_host = (idx == s_hostidx);
        for (int i = idx; i < s_memcount - 1; i++) nl_cpy(s_members[i], s_members[i + 1], NL_NAMELEN);
        s_memcount--;
        if (s_memcount == 0) {
            printf("netlab: %s left; pod %s dissolved.\n", me, s_pod);
            s_pod[0] = 0; s_hostidx = -1;
            s_listen_name[0] = 0; s_atk_name[0] = 0; s_victim_name[0] = 0;
            nl_sb_reset();
            s_isolated = 0;   /* no pod, no binding: never leave the net paused */
            return 0;
        }
        nl_pick_host();
        printf("netlab: %s left.\n", me);
        if (s_hostidx >= 0) printf("  host %s: %s\n", was_host ? "succession" : "remains", s_members[s_hostidx]);
        else printf("  host pending: every remaining member is the real server host.\n");
        if (!strcmp(me, s_atk_name) || !strcmp(me, s_victim_name)) {
            s_atk_name[0] = 0; s_victim_name[0] = 0;
            nl_sb_reset();
            s_isolated = 0;
            printf("netlab: your reverse-shell binding ended because you left the pod.\n");
        }
        if (!strcmp(me, s_listen_name)) s_listen_name[0] = 0;
        return 0;
    }
    fprintf(stderr, "netlab: usage: netlab pod create|join|members|host|leave\n");
    return 1;
}

static int verb_listen(int argc, char **argv) {
    const char *me;
    fl_session_init();
    me = fl_session_current_user();
    if (!s_pod[0] || nl_member_idx(me) < 0) {
        fprintf(stderr, "netlab: join a pod first (`netlab pod create` or `netlab pod join`).\n");
        return 1;
    }
    if (argc >= 3 && !strcmp(argv[2], "cancel")) {
        if (strcmp(s_listen_name, me)) { fprintf(stderr, "netlab: you have no open listener to cancel.\n"); return 1; }
        s_listen_name[0] = 0;
        printf("netlab: listener cancelled.\n");
        return 0;
    }
    if (s_atk_name[0]) { fprintf(stderr, "netlab: a reverse shell is already bound in this pod; `netlab end` it first.\n"); return 1; }
    if (s_listen_name[0]) {
        fprintf(stderr, "netlab: a listener is already open in this pod. `netlab accept` it, or its\n");
        fprintf(stderr, "  owner can `netlab listen cancel`.\n");
        return 1;
    }
    nl_cpy(s_listen_name, me, NL_NAMELEN);
    printf("netlab: %s is listening in pod %s.\n", me, s_pod);
    printf("  Another pod member can `netlab accept` to bind as the victim side\n");
    printf("  (SIMULATED -- a disposable isolated sandbox, never their real account).\n");
    return 0;
}

static int verb_accept(void) {
    const char *me;
    fl_session_init();
    me = fl_session_current_user();
    if (!s_pod[0] || nl_member_idx(me) < 0) { fprintf(stderr, "netlab: join a pod first.\n"); return 1; }
    if (!s_listen_name[0]) { fprintf(stderr, "netlab: no open listener in this pod. Someone runs `netlab listen` first.\n"); return 1; }
    if (!strcmp(s_listen_name, me)) { fprintf(stderr, "netlab: you cannot accept your own listener.\n"); return 1; }
    if (s_atk_name[0]) { fprintf(stderr, "netlab: already bound; `netlab end` it first.\n"); return 1; }
    /* "when we switch to lab network": re-resolve the pod host here, the
     * real isolation moment, since the real server host may have changed
     * since the pod was created or last joined. */
    nl_pick_host();
    nl_cpy(s_atk_name, s_listen_name, NL_NAMELEN);
    nl_cpy(s_victim_name, me, NL_NAMELEN);
    s_listen_name[0] = 0;
    nl_sb_reset();
    s_isolated = 1;
    printf("netlab: %s accepted. %s is bound as attacker against a fresh, isolated SANDBOX\n", me, s_atk_name);
    printf("  labeled for %s -- not %s's real session or real files.\n", me, me);
    printf("  %s: run `netlab <cmd>` to execute in the sandbox. Either side can\n", s_atk_name);
    printf("  `netlab end` it at any time. The real `server` command is paused while open.\n");
    return 0;
}

static int verb_end(void) {
    const char *me;
    fl_session_init();
    me = fl_session_current_user();
    if (strcmp(me, s_atk_name) && strcmp(me, s_victim_name)) {
        printf("netlab: no reverse shell is bound to you.\n");
        return 0;
    }
    printf("netlab: reverse shell between %s and %s closed. real network restored.\n", s_atk_name, s_victim_name);
    s_atk_name[0] = 0; s_victim_name[0] = 0;
    nl_sb_reset();
    s_isolated = 0;
    return 0;
}

static int verb_status(void) {
    fl_session_init();
    printf("netlab status:\n");
    printf("  pod:  %s\n", s_pod[0] ? s_pod : "(none)");
    if (s_pod[0]) printf("  host: %s   members: %d\n", s_hostidx >= 0 ? s_members[s_hostidx] : "(pending)", s_memcount);
    printf("  listener: %s\n", s_listen_name[0] ? s_listen_name : "(none)");
    if (s_atk_name[0]) printf("  reverse shell: OPEN  %s -> sandbox(%s) [simulated]\n", s_atk_name, s_victim_name);
    else                printf("  reverse shell: closed\n");
    return 0;
}

int cmd_netlab_run(int argc, char **argv) {
    const char *verb;
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") ||
        !strcmp(argv[1], "--help")) {
        print_usage();
        return (argc < 2) ? 0 : 0;
    }
    verb = argv[1];

    if (!strcmp(verb, "pod"))    return verb_pod(argc, argv);
    if (!strcmp(verb, "listen")) return verb_listen(argc, argv);
    if (!strcmp(verb, "accept")) return verb_accept();
    if (!strcmp(verb, "end"))    return verb_end();
    if (!strcmp(verb, "status")) return verb_status();

    if (!strcmp(verb, "consent"))
        return verb_consent();
    if (!strcmp(verb, "pods"))
        return verb_pods();
    if (!strcmp(verb, "rs-explain"))
        return verb_rs_explain();
    if (!strcmp(verb, "defend"))
        return verb_defend();

    if (!strcmp(verb, "hosts") || !strcmp(verb, "topology") ||
        !strcmp(verb, "arp-demo") || !strcmp(verb, "monitor") ||
        !strcmp(verb, "scan") || !strcmp(verb, "wpa")) {
        const nl_pod_t *p;
        if (argc < 3) {
            fprintf(stderr, "netlab: %s needs a pod (name or id). Try `netlab pods`.\n", verb);
            return 1;
        }
        p = find_pod(argv[2]);
        if (!p) {
            fprintf(stderr, "netlab: no pod '%s'. Try `netlab pods`.\n", argv[2]);
            return 1;
        }
        if (!strcmp(verb, "hosts"))    return verb_hosts(p);
        if (!strcmp(verb, "scan"))     return verb_scan(p);
        if (!strcmp(verb, "wpa"))      return verb_wpa(p);
        if (!strcmp(verb, "topology")) return verb_topology(p);
        if (!strcmp(verb, "arp-demo")) return verb_arp_demo(p);
        /* monitor [-c N] */
        {
            long limit = -1;
            if (argc >= 5 && !strcmp(argv[3], "-c"))
                limit = strtol(argv[4], NULL, 10);
            return verb_monitor(p, limit);
        }
    }

    if (!strcmp(verb, "traceroute")) {
        const nl_pod_t *p;
        if (argc < 4) {
            fprintf(stderr, "netlab: traceroute needs <pod> <dst>. Try `netlab hosts <pod>`.\n");
            return 1;
        }
        p = find_pod(argv[2]);
        if (!p) {
            fprintf(stderr, "netlab: no pod '%s'. Try `netlab pods`.\n", argv[2]);
            return 1;
        }
        return verb_traceroute(p, argv[3]);
    }

    if (!strcmp(verb, "pcap")) {
        if (argc >= 5 && !strcmp(argv[2], "write")) {
            const nl_pod_t *p = find_pod(argv[3]);
            if (!p) {
                fprintf(stderr, "netlab: no pod '%s'. Try `netlab pods`.\n", argv[3]);
                return 1;
            }
            return pcap_write(p, argv[4]);
        }
        if (argc >= 4 && !strcmp(argv[2], "analyze"))
            return pcap_analyze(argv[3]);
        fprintf(stderr, "netlab: usage: netlab pcap write <pod> <file> | netlab pcap analyze <file>\n");
        return 1;
    }

    /* Reverse-shell redirection: while a binding is open, the bound attacker's
     * non-management `netlab <cmd>` runs for real against the disposable
     * sandbox (never the victim's real account). Only the attacker's own
     * calls are redirected; the victim keeps using its real shell untouched. */
    if (s_atk_name[0] && !nl_is_mgmt_verb(verb)) {
        fl_session_init();
        if (!strcmp(fl_session_current_user(), s_atk_name)) {
            nl_sb_run(argc, argv);
            return 0;
        }
    }

    fprintf(stderr, "netlab: unknown verb '%s'. Try `netlab help`.%s\n", verb,
            s_atk_name[0] ? "" : " (reverse shell: `netlab listen` / `netlab accept`)");
    return 1;
}

/* Batch arity: how many tokens (including "netlab") this invocation owns.
 * This MUST match cmd_netlab_run()'s own argv parsing exactly, because the
 * shell uses it to segment a multi-command line — a generic "stop at the next
 * command name" heuristic mis-splits verbs/args that happen to be command
 * names (e.g. `pcap write <file>`, where `write` is itself a command). */
int cmd_netlab_batch_tokens_count(int argc, char **argv, int i) {
    int rem = argc - i;                 /* tokens available, incl. "netlab" */
    const char *v;
    if (rem < 2)
        return rem < 1 ? 1 : rem;       /* bare "netlab" */
    v = argv[i + 1];
    if (!strcmp(v, "help") || !strcmp(v, "-h") || !strcmp(v, "--help") ||
        !strcmp(v, "consent") || !strcmp(v, "pods") || !strcmp(v, "defend") ||
        !strcmp(v, "rs-explain") || !strcmp(v, "accept") || !strcmp(v, "end") ||
        !strcmp(v, "status"))
        return 2;
    if (!strcmp(v, "hosts") || !strcmp(v, "topology") || !strcmp(v, "arp-demo") ||
        !strcmp(v, "scan") || !strcmp(v, "wpa"))
        return rem < 3 ? rem : 3;
    if (!strcmp(v, "traceroute"))
        return rem < 4 ? rem : 4;
    if (!strcmp(v, "listen")) {
        const char *sub = (rem >= 3) ? argv[i + 2] : NULL;
        if (sub && !strcmp(sub, "cancel"))
            return 3;                   /* listen cancel */
        return 2;                       /* listen */
    }
    if (!strcmp(v, "pod")) {
        const char *sub = (rem >= 3) ? argv[i + 2] : NULL;
        if (sub && !strcmp(sub, "create"))
            return rem < 4 ? rem : 4;   /* pod create <pod> */
        return rem < 3 ? rem : 3;       /* pod join|leave|members|host */
    }
    if (!strcmp(v, "monitor")) {
        if (rem >= 5 && !strcmp(argv[i + 3], "-c"))
            return 5;                   /* monitor <pod> -c N */
        return rem < 3 ? rem : 3;       /* monitor <pod> */
    }
    if (!strcmp(v, "pcap")) {
        const char *sub = (rem >= 3) ? argv[i + 2] : NULL;
        if (sub && !strcmp(sub, "write"))
            return rem < 5 ? rem : 5;   /* pcap write <pod> <file> */
        if (sub && !strcmp(sub, "analyze"))
            return rem < 4 ? rem : 4;   /* pcap analyze <file> */
        return rem < 3 ? rem : 3;
    }
    return 2;                           /* unknown verb: name + verb */
}
