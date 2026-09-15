#include "labnet.h"

#define ARP_MAX 4
#define UDP_MAX 4

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static unsigned parse_u(const char *s)
{
    unsigned n = 0;
    if (!s)
        return 0;
    while (*s >= '0' && *s <= '9')
        n = n * 10u + (unsigned)(*s++ - '0');
    return n;
}

struct arp {
    int used;
    char ip[16];
    char mac[20];
};

struct udp {
    int used;
    unsigned port;
    char last[48];
};

static struct arp s_arp[ARP_MAX];
static struct udp s_udp[UDP_MAX];
static unsigned s_lo_rx, s_lo_tx, s_lab_rx, s_lab_tx;
static char s_wifi_ssid[24];
static int s_wifi_up;
static char s_wifi_known[2][24];

void fl_fs_labnet_init(void)
{
    for (int i = 0; i < ARP_MAX; ++i)
        s_arp[i].used = 0;
    for (int i = 0; i < UDP_MAX; ++i)
        s_udp[i].used = 0;
    s_lo_rx = s_lo_tx = s_lab_rx = s_lab_tx = 0;
    s_wifi_up = 0;
    s_wifi_ssid[0] = 0;
    str_copy(s_wifi_known[0], "FlintstoneLab", sizeof(s_wifi_known[0]));
    str_copy(s_wifi_known[1], "OpenLab", sizeof(s_wifi_known[1]));
    s_arp[0].used = 1;
    str_copy(s_arp[0].ip, "10.0.0.1", sizeof(s_arp[0].ip));
    str_copy(s_arp[0].mac, "aa:bb:cc:dd:ee:01", sizeof(s_arp[0].mac));
    s_arp[1].used = 1;
    str_copy(s_arp[1].ip, "10.0.0.2", sizeof(s_arp[1].ip));
    str_copy(s_arp[1].mac, "aa:bb:cc:dd:ee:02", sizeof(s_arp[1].mac));
}

void fl_fs_labnet_ifconfig(void (*emit)(const char *), void (*emit_uint)(unsigned))
{
    emit("lo UP 127.0.0.1 rx ");
    emit_uint(s_lo_rx);
    emit(" tx ");
    emit_uint(s_lo_tx);
    emit("\r\nlab0 UP 10.0.0.2 rx ");
    emit_uint(s_lab_rx);
    emit(" tx ");
    emit_uint(s_lab_tx);
    emit("\r\n");
}

void fl_fs_labnet_arp(const char *op, const char *ip, const char *mac,
                      void (*emit)(const char *), void (*emit_uint)(unsigned))
{
    (void)emit_uint;
    if (op && str_eq(op, "-s") && ip && mac) {
        int slot = -1;
        for (int i = 0; i < ARP_MAX; ++i) {
            if (s_arp[i].used && str_eq(s_arp[i].ip, ip)) {
                slot = i;
                break;
            }
            if (slot < 0 && !s_arp[i].used)
                slot = i;
        }
        if (slot < 0) {
            emit("arp full\r\n");
            return;
        }
        s_arp[slot].used = 1;
        str_copy(s_arp[slot].ip, ip, sizeof(s_arp[slot].ip));
        str_copy(s_arp[slot].mac, mac, sizeof(s_arp[slot].mac));
        emit("arp set\r\n");
        return;
    }
    if (op && str_eq(op, "-d") && ip) {
        for (int i = 0; i < ARP_MAX; ++i) {
            if (s_arp[i].used && str_eq(s_arp[i].ip, ip)) {
                s_arp[i].used = 0;
                emit("arp deleted\r\n");
                return;
            }
        }
        emit("arp not found\r\n");
        return;
    }
    for (int i = 0; i < ARP_MAX; ++i) {
        if (!s_arp[i].used)
            continue;
        emit(s_arp[i].ip);
        emit(" ");
        emit(s_arp[i].mac);
        emit("\r\n");
    }
}

void fl_fs_labnet_route(void (*emit)(const char *))
{
    emit("default via 10.0.0.1 dev lab0\r\n");
    emit("127.0.0.0/8 dev lo\r\n");
    emit("10.0.0.0/24 dev lab0\r\n");
}

void fl_fs_labnet_netstat(void (*emit)(const char *), void (*emit_uint)(unsigned))
{
    int any = 0;
    for (int i = 0; i < UDP_MAX; ++i) {
        if (!s_udp[i].used)
            continue;
        emit("udp :");
        emit_uint(s_udp[i].port);
        emit("\r\n");
        any = 1;
    }
    if (!any)
        emit("udp none\r\n");
}

int fl_fs_labnet_resolve(const char *host, char *v4, unsigned v4cap, char *v6, unsigned v6cap)
{
    if (!host || !host[0])
        return -1;
    if (str_eq(host, "localhost") || str_eq(host, "127.0.0.1") || str_eq(host, "::1")) {
        if (v4)
            str_copy(v4, "127.0.0.1", v4cap);
        if (v6)
            str_copy(v6, "::1", v6cap);
        return 0;
    }
    if (str_eq(host, "flintstone") || str_eq(host, "10.0.0.2") || str_eq(host, "lab0")) {
        if (v4)
            str_copy(v4, "10.0.0.2", v4cap);
        if (v6)
            str_copy(v6, "fe80::2", v6cap);
        return 0;
    }
    if (str_eq(host, "10.0.0.1") || str_eq(host, "gateway")) {
        if (v4)
            str_copy(v4, "10.0.0.1", v4cap);
        if (v6)
            str_copy(v6, "fe80::1", v6cap);
        return 0;
    }
    return -1;
}

int fl_fs_labnet_ping(const char *host, unsigned port, int v6,
                      void (*emit)(const char *), void (*emit_uint)(unsigned))
{
    char v4[16];
    char v6addr[16];
    if (fl_fs_labnet_resolve(host, v4, sizeof(v4), v6addr, sizeof(v6addr)) != 0) {
        emit("ping: unknown host ");
        emit(host ? host : "");
        emit("\r\n");
        return -1;
    }
    if (v6)
        s_lo_tx++, s_lo_rx++;
    else if (v4[0] == '1')
        s_lo_tx++, s_lo_rx++;
    else
        s_lab_tx++, s_lab_rx++;
    emit(v6 ? "PING6 " : "PING ");
    emit(host);
    emit(" ");
    emit(v6 ? v6addr : v4);
    if (port) {
        emit(" tcp ");
        emit_uint(port);
        emit(" open");
    } else {
        emit(" 64 bytes time=0ms");
    }
    emit("\r\n");
    return 0;
}

int fl_fs_labnet_check(const char *host, unsigned port,
                       void (*emit)(const char *), void (*emit_uint)(unsigned))
{
    char v4[16];
    char v6[16];
    if (fl_fs_labnet_resolve(host, v4, sizeof(v4), v6, sizeof(v6)) != 0) {
        emit("check FAIL ");
        emit(host ? host : "");
        emit("\r\n");
        return -1;
    }
    emit("check OK ");
    emit(host);
    emit(" ");
    emit(v4);
    emit(":");
    emit_uint(port);
    emit("\r\n");
    return 0;
}

int fl_fs_labnet_udpsend(const char *endpoint, const char *msg)
{
    unsigned port = 0;
    const char *colon;
    int slot = -1;
    if (!endpoint || !msg)
        return -1;
    colon = endpoint;
    while (*colon && *colon != ':')
        ++colon;
    if (*colon == ':')
        port = parse_u(colon + 1);
    if (port == 0)
        return -1;
    for (int i = 0; i < UDP_MAX; ++i) {
        if (s_udp[i].used && s_udp[i].port == port) {
            slot = i;
            break;
        }
        if (slot < 0 && !s_udp[i].used)
            slot = i;
    }
    if (slot < 0)
        return -1;
    s_udp[slot].used = 1;
    s_udp[slot].port = port;
    str_copy(s_udp[slot].last, msg, sizeof(s_udp[slot].last));
    s_lab_tx++;
    return 0;
}

int fl_fs_labnet_udplisten(unsigned port, char *out, unsigned cap)
{
    if (port == 0 || !out || cap == 0)
        return -1;
    for (int i = 0; i < UDP_MAX; ++i) {
        if (s_udp[i].used && s_udp[i].port == port) {
            str_copy(out, s_udp[i].last, cap);
            s_lab_rx++;
            return 0;
        }
    }
    for (int i = 0; i < UDP_MAX; ++i) {
        if (s_udp[i].used)
            continue;
        s_udp[i].used = 1;
        s_udp[i].port = port;
        s_udp[i].last[0] = 0;
        out[0] = 0;
        return 0;
    }
    return -1;
}

void fl_fs_labnet_wifi(const char *op, const char *arg, void (*emit)(const char *))
{
    if (!op || !op[0] || str_eq(op, "scan")) {
        emit("wifi scan FlintstoneLab OpenLab\r\n");
        return;
    }
    if (str_eq(op, "status")) {
        emit("wifi ");
        emit(s_wifi_up ? "associated " : "down");
        if (s_wifi_up)
            emit(s_wifi_ssid);
        emit("\r\n");
        return;
    }
    if (str_eq(op, "known")) {
        emit(s_wifi_known[0]);
        emit("\r\n");
        emit(s_wifi_known[1]);
        emit("\r\n");
        return;
    }
    if (str_eq(op, "leave")) {
        s_wifi_up = 0;
        s_wifi_ssid[0] = 0;
        emit("wifi left\r\n");
        return;
    }
    if (str_eq(op, "join")) {
        if (!arg || !arg[0]) {
            emit("usage: wifi join <ssid>\r\n");
            return;
        }
        str_copy(s_wifi_ssid, arg, sizeof(s_wifi_ssid));
        s_wifi_up = 1;
        emit("wifi joined ");
        emit(s_wifi_ssid);
        emit("\r\n");
        return;
    }
    emit("wifi scan|join|leave|status|known\r\n");
}
