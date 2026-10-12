#include "commands.h"
#include "identity.h"
#include "labdisk.h"
#include "labnet.h"
#include "ramfs.h"
#include "serial.h"
#include "vga.h"

#define AUDIT_MAX 8
#define LOC_MAX 8
#define AUDIT_LINE 80

static char s_redir[FL_FS_RAMFS_PATH];
static char s_audit[AUDIT_MAX][AUDIT_LINE];
static unsigned s_audit_n;
static char s_loc[LOC_MAX][FL_FS_RAMFS_PATH];
static unsigned s_loc_n;

/*
 * Lab network mode. 0 = live: the browser relay (the real external network)
 * is allowed. 1 = lab-isolated: a netlab MitM+RS session is running, so the
 * real network is dropped — `server` relay verbs refuse here, and the glue in
 * tools/browser-lab/lab.js disconnects the relay on the LAB_NETMODE token.
 * `netlab start` enters it; `netlab stop` restores live and the server resumes.
 */
static int s_lab_netmode; /* 0 live/real, 1 lab-isolated */

/*
 * netlab session state (this lab seat). Pods are keyed on REAL session
 * identity (fl_fs_identity_user(session)) rather than a free-typed name, so
 * "Flinstone" in a pod really is whichever real account is logged into that
 * session; a second concurrent session logged into the same account displays
 * as "flinstone {2}". The reverse shell is still a SIMULATION: accepting a
 * listener binds the attacker session to a fresh, isolated, disposable
 * sandbox labeled with the accepting session's identity — never that
 * session's real terminal, real ramfs, or real files. The accepting party
 * can end the binding at any time.
 */
#define NL_NAMELEN 24
#define NL_MAXMEM  FL_FS_MAX_SESSIONS /* one pod seat per real session */
static char s_nl_pod[NL_NAMELEN];
static int  s_nl_members[NL_MAXMEM];  /* real session numbers, join order    */
static int  s_nl_memcount;
static int  s_nl_hostidx;             /* index into s_nl_members, -1=pending */

/*
 * Best-effort local signal: the session that most recently had US actually
 * forward `server host` to the real relay (i.e. the isolation check passed
 * and SERVER_RELAY host was sent). This cannot see the relay's live roster,
 * so it assumes the common case — the first host request is granted — and
 * is cleared on this session's own `server leave`/`kill`. It is used only to
 * keep one identity from being asked to relay both the pod's lab traffic and
 * the real server's traffic at once.
 */
static int  s_nl_acting_host_session = -1;

static int  s_nl_listen_session = -1; /* open listener owner, -1 = none      */
static int  s_nl_atk_session = -1;    /* bound attacker session, -1 = unbound */
static int  s_nl_victim_session = -1; /* bound accepting session             */

/* Disposable per-binding sandbox: a tiny isolated file table. Reset on every
 * new accept() and on end(). Never backed by the real ramfs. */
#define NL_SB_FILES 8
#define NL_SB_NAMELEN 24
#define NL_SB_CONTENTLEN 48
static char s_nl_sb_name[NL_SB_FILES][NL_SB_NAMELEN];
static char s_nl_sb_content[NL_SB_FILES][NL_SB_CONTENTLEN];
static int  s_nl_sb_used[NL_SB_FILES];
static int  s_nl_sb_count;

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

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    while (s[n])
        ++n;
    return n;
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

static void skip_spaces(const char **cursor)
{
    while (**cursor == ' ')
        ++*cursor;
}

static int take_word(const char **cursor, char *out, unsigned cap)
{
    unsigned n = 0;
    skip_spaces(cursor);
    if (**cursor == 0)
        return 0;
    while (**cursor && **cursor != ' ' && n + 1 < cap)
        out[n++] = *(*cursor)++;
    out[n] = 0;
    return 1;
}

static void redir_append(const char *s)
{
    char cur[FL_FS_RAMFS_DATA];
    unsigned n;
    if (!s_redir[0] || !s)
        return;
    cur[0] = 0;
    (void)fl_fs_ramfs_cat(0, s_redir, cur, sizeof(cur));
    n = str_len(cur);
    while (*s && n + 1 < sizeof(cur)) {
        if (*s != '\r')
            cur[n++] = *s;
        ++s;
    }
    cur[n] = 0;
    (void)fl_fs_ramfs_write(0, s_redir, cur);
}

static void emit(const char *s)
{
    const char *p = s;
    while (*p) {
        fl_fs_serial_putc(*p);
        fl_fs_vga_putc(*p);
        ++p;
    }
    redir_append(s);
}

static void emit_uint(unsigned value)
{
    char buf[10];
    int n = 0;
    if (value == 0) {
        emit("0");
        return;
    }
    while (value && n < 10) {
        buf[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n--) {
        char c[2] = { buf[n], 0 };
        emit(c);
    }
}

static void loc_add(const char *path)
{
    unsigned slot;
    if (!path || !path[0])
        return;
    if (s_loc_n >= LOC_MAX) {
        for (unsigned i = 1; i < LOC_MAX; ++i)
            str_copy(s_loc[i - 1], s_loc[i], FL_FS_RAMFS_PATH);
        slot = LOC_MAX - 1;
    } else {
        slot = s_loc_n++;
    }
    str_copy(s_loc[slot], path, FL_FS_RAMFS_PATH);
}

static void audit_add(const char *verb, const char *rest)
{
    char line[AUDIT_LINE];
    unsigned n = 0;
    unsigned slot;
    while (verb[n] && n + 1 < sizeof(line)) {
        line[n] = verb[n];
        ++n;
    }
    if (rest && rest[0] && n + 1 < sizeof(line)) {
        line[n++] = ' ';
        while (*rest && n + 1 < sizeof(line))
            line[n++] = *rest++;
    }
    line[n] = 0;
    if (s_audit_n >= AUDIT_MAX) {
        for (unsigned i = 1; i < AUDIT_MAX; ++i)
            str_copy(s_audit[i - 1], s_audit[i], AUDIT_LINE);
        slot = AUDIT_MAX - 1;
    } else {
        slot = s_audit_n++;
    }
    str_copy(s_audit[slot], line, AUDIT_LINE);
}

static void fs_status(int rc)
{
    if (rc == 0)
        return;
    if (rc == -1)
        emit("not found\r\n");
    else if (rc == -2)
        emit("not a directory\r\n");
    else if (rc == -3)
        emit("already exists\r\n");
    else if (rc == -4)
        emit("ramfs full\r\n");
    else if (rc == -5)
        emit("not a file\r\n");
    else if (rc == -7)
        emit("directory not empty\r\n");
    else
        emit("bad path\r\n");
}

static void dir_visit(const char *name, int is_dir, unsigned size, void *ctx)
{
    (void)ctx;
    emit(is_dir ? "d " : "f ");
    emit(name);
    if (!is_dir) {
        emit(" ");
        emit_uint(size);
    }
    emit("\r\n");
}

static void path_visit(const char *path, void *ctx)
{
    (void)ctx;
    emit(path);
    emit("\r\n");
}

static int cluster_text(const char **cursor, char *out, unsigned cap)
{
    char flag[4];
    skip_spaces(cursor);
    out[0] = 0;
    if (!take_word(cursor, flag, sizeof(flag)))
        return 0;
    if (str_eq(flag, "-t") || str_eq(flag, "-h")) {
        skip_spaces(cursor);
        str_copy(out, *cursor, cap);
        return 1;
    }
    str_copy(out, flag, cap);
    skip_spaces(cursor);
    if (**cursor) {
        unsigned n = str_len(out);
        if (n + 1 < cap)
            out[n++] = ' ';
        str_copy(out + n, *cursor, cap > n ? cap - n : 0);
    }
    return 1;
}

static int run_fs(int session, const char *verb, const char **cursor)
{
    char path[FL_FS_RAMFS_PATH];
    char path2[FL_FS_RAMFS_PATH];
    if (str_eq(verb, "pwd")) {
        fl_fs_ramfs_pwd(session, path, sizeof(path));
        emit(path);
        emit("\r\n");
        return 1;
    }
    if (str_eq(verb, "cd")) {
        if (!take_word(cursor, path, sizeof(path))) {
            fl_fs_ramfs_pwd(session, path, sizeof(path));
            emit(path);
            emit("\r\n");
            return 1;
        }
        {
            int rc = fl_fs_ramfs_cd(session, path);
            if (rc == 0)
                loc_add(path);
            fs_status(rc);
        }
        return 1;
    }
    if (str_eq(verb, "dir") || str_eq(verb, "ls")) {
        int rc;
        if (!take_word(cursor, path, sizeof(path)))
            path[0] = 0;
        rc = fl_fs_ramfs_dir(session, path, dir_visit, 0);
        if (rc == 0)
            return 1;
        fs_status(rc);
        return 1;
    }
    if (str_eq(verb, "listdirs")) {
        if (!take_word(cursor, path, sizeof(path)))
            path[0] = 0;
        fs_status(fl_fs_ramfs_listdirs(session, path, dir_visit, 0));
        return 1;
    }
    if (str_eq(verb, "mkdir")) {
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: mkdir <dir>\r\n");
            return 1;
        }
        {
            int rc = fl_fs_ramfs_mkdir(session, path);
            if (rc == 0)
                loc_add(path);
            fs_status(rc);
        }
        return 1;
    }
    if (str_eq(verb, "rm") || str_eq(verb, "rmdir") || str_eq(verb, "rmtree")) {
        int rc;
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: ");
            emit(verb);
            emit(" <path>\r\n");
            return 1;
        }
        if (str_eq(verb, "rmdir"))
            rc = fl_fs_ramfs_rmdir(session, path);
        else if (str_eq(verb, "rmtree"))
            rc = fl_fs_ramfs_rmtree(session, path);
        else
            rc = fl_fs_ramfs_rm(session, path);
        if (rc == 0)
            loc_add(path);
        fs_status(rc);
        return 1;
    }
    if (str_eq(verb, "mv")) {
        if (!take_word(cursor, path, sizeof(path)) || !take_word(cursor, path2, sizeof(path2))) {
            emit("usage: mv <src> <dst>\r\n");
            return 1;
        }
        {
            int rc = fl_fs_ramfs_mv(session, path, path2);
            if (rc == 0)
                loc_add(path2);
            fs_status(rc);
        }
        return 1;
    }
    if (str_eq(verb, "cat") || str_eq(verb, "type")) {
        char data[FL_FS_RAMFS_DATA];
        int rc;
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: cat <file>\r\n");
            return 1;
        }
        rc = fl_fs_ramfs_cat(session, path, data, sizeof(data));
        if (rc != 0) {
            fs_status(rc);
            return 1;
        }
        loc_add(path);
        emit(data);
        emit("\r\n");
        return 1;
    }
    if (str_eq(verb, "write") || str_eq(verb, "make")) {
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: ");
            emit(verb);
            emit(" <file> [text]\r\n");
            return 1;
        }
        skip_spaces(cursor);
        {
            int rc = fl_fs_ramfs_write(session, path, **cursor ? *cursor : "");
            if (rc == 0) {
                loc_add(path);
                emit("wrote ");
                emit(path);
                emit("\r\n");
            } else {
                fs_status(rc);
            }
        }
        return 1;
    }
    if (str_eq(verb, "search")) {
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: search <text>\r\n");
            return 1;
        }
        (void)fl_fs_ramfs_search(session, path, path_visit, 0);
        (void)fl_fs_labdisk_search(path, path_visit, 0);
        return 1;
    }
    if (str_eq(verb, "du")) {
        unsigned rbytes = 0, nodes = 0, dbytes = 0, clusters = 0;
        if (!take_word(cursor, path, sizeof(path)))
            path[0] = 0;
        if (fl_fs_ramfs_du(session, path, &rbytes, &nodes) != 0) {
            emit("not found\r\n");
            return 1;
        }
        fl_fs_labdisk_usage(&dbytes, &clusters);
        emit("ramfs ");
        emit_uint(rbytes);
        emit("b nodes ");
        emit_uint(nodes);
        emit(" disk ");
        emit_uint(dbytes);
        emit("b clusters ");
        emit_uint(clusters);
        emit("\r\n");
        return 1;
    }
    if (str_eq(verb, "loc") || str_eq(verb, "where")) {
        unsigned want = 16;
        char arg[8];
        if (take_word(cursor, arg, sizeof(arg)))
            want = parse_u(arg);
        if (want == 0)
            want = 16;
        if (s_loc_n == 0) {
            emit("path log empty\r\n");
            return 1;
        }
        {
            unsigned start = s_loc_n > want ? s_loc_n - want : 0;
            for (unsigned i = start; i < s_loc_n; ++i) {
                emit_uint(i + 1);
                emit(": ");
                emit(s_loc[i]);
                emit("\r\n");
            }
        }
        return 1;
    }
    return 0;
}

static int run_disk(int session, const char *verb, const char **cursor)
{
    char a[FL_FS_RAMFS_PATH];
    char b[FL_FS_RAMFS_PATH];
    char c[FL_FS_RAMFS_DATA];
    if (str_eq(verb, "createdisk")) {
        char rows[8], nib[8];
        if (!take_word(cursor, a, sizeof(a)) || !take_word(cursor, rows, sizeof(rows))) {
            emit("usage: createdisk <volume> <rows> <nibbles>\r\n");
            return 1;
        }
        if (!take_word(cursor, nib, sizeof(nib)))
            nib[0] = '8', nib[1] = 0;
        if (fl_fs_labdisk_create(a, parse_u(rows), parse_u(nib)) != 0)
            emit("createdisk failed\r\n");
        else {
            emit("created disk ");
            emit(a);
            emit("\r\n");
        }
        return 1;
    }
    if (str_eq(verb, "format")) {
        char vol[16], rows[8], nib[8];
        if (!take_word(cursor, a, sizeof(a)) || !take_word(cursor, vol, sizeof(vol))) {
            emit("usage: format <disk> <volume> <rows> <nibbles>\r\n");
            return 1;
        }
        if (!take_word(cursor, rows, sizeof(rows)))
            str_copy(rows, "4", sizeof(rows));
        if (!take_word(cursor, nib, sizeof(nib)))
            str_copy(nib, "8", sizeof(nib));
        if (fl_fs_labdisk_format(a, vol, parse_u(rows), parse_u(nib)) == 0)
            emit("formatted\r\n");
        else
            emit("format failed\r\n");
        return 1;
    }
    if (str_eq(verb, "setdisk")) {
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: setdisk <disk>\r\n");
            return 1;
        }
        if (fl_fs_labdisk_set(a) == 0)
            emit("setdisk ok\r\n");
        else
            emit("setdisk failed\r\n");
        return 1;
    }
    if (str_eq(verb, "initdisk")) {
        char n[8], sz[8];
        if (!take_word(cursor, n, sizeof(n))) {
            emit("usage: initdisk <count> <size>\r\n");
            return 1;
        }
        if (!take_word(cursor, sz, sizeof(sz)))
            str_copy(sz, "32", sizeof(sz));
        if (fl_fs_labdisk_initgeom(parse_u(n), parse_u(sz)) == 0)
            emit("initdisk ok\r\n");
        else
            emit("initdisk failed\r\n");
        return 1;
    }
    if (str_eq(verb, "printdisk")) {
        fl_fs_labdisk_print(emit, emit_uint);
        return 1;
    }
    if (str_eq(verb, "listclusters")) {
        fl_fs_labdisk_list(emit, emit_uint);
        return 1;
    }
    if (str_eq(verb, "writecluster") || str_eq(verb, "update")) {
        char idx[8];
        if (!take_word(cursor, idx, sizeof(idx))) {
            emit("usage: writecluster <idx> -t|-h <data>\r\n");
            return 1;
        }
        if (str_eq(verb, "update"))
            (void)fl_fs_labdisk_del_cluster(parse_u(idx));
        cluster_text(cursor, c, sizeof(c));
        if (fl_fs_labdisk_write_cluster(parse_u(idx), c) == 0)
            emit("cluster written\r\n");
        else
            emit("cluster failed\r\n");
        return 1;
    }
    if (str_eq(verb, "delcluster")) {
        char idx[8];
        if (!take_word(cursor, idx, sizeof(idx))) {
            emit("usage: delcluster <idx>\r\n");
            return 1;
        }
        if (fl_fs_labdisk_del_cluster(parse_u(idx)) == 0)
            emit("cluster deleted\r\n");
        else
            emit("cluster failed\r\n");
        return 1;
    }
    if (str_eq(verb, "addcluster")) {
        cluster_text(cursor, c, sizeof(c));
        if (fl_fs_labdisk_add_cluster(c) == 0)
            emit("cluster added\r\n");
        else
            emit("cluster failed\r\n");
        return 1;
    }
    if (str_eq(verb, "diskfiles")) {
        (void)take_word(cursor, a, sizeof(a));
        fl_fs_labdisk_files(dir_visit, 0);
        return 1;
    }
    if (str_eq(verb, "diskmkdir")) {
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: diskmkdir <path>\r\n");
            return 1;
        }
        if (fl_fs_labdisk_mkdir(a) == 0)
            emit("disk mkdir\r\n");
        else
            emit("diskmkdir failed\r\n");
        return 1;
    }
    if (str_eq(verb, "diskdel")) {
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: diskdel <path>\r\n");
            return 1;
        }
        if (fl_fs_labdisk_del_file(a) == 0)
            emit("diskdel ok\r\n");
        else
            emit("diskdel failed\r\n");
        return 1;
    }
    if (str_eq(verb, "diskput")) {
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: diskput <file> [path]\r\n");
            return 1;
        }
        if (!take_word(cursor, b, sizeof(b)))
            str_copy(b, a, sizeof(b));
        if (fl_fs_ramfs_cat(session, a, c, sizeof(c)) != 0) {
            emit("not found\r\n");
            return 1;
        }
        if (fl_fs_labdisk_put(b, c) == 0) {
            loc_add(b);
            emit("diskput ok\r\n");
        } else {
            emit("diskput failed\r\n");
        }
        return 1;
    }
    if (str_eq(verb, "diskget")) {
        if (!take_word(cursor, a, sizeof(a)) || !take_word(cursor, b, sizeof(b))) {
            emit("usage: diskget <path> <file>\r\n");
            return 1;
        }
        if (fl_fs_labdisk_get(a, c, sizeof(c)) != 0) {
            emit("not found\r\n");
            return 1;
        }
        if (fl_fs_ramfs_write(session, b, c) == 0) {
            loc_add(b);
            emit("diskget ok\r\n");
        } else {
            fs_status(-4);
        }
        return 1;
    }
    if (str_eq(verb, "import")) {
        if (!take_word(cursor, a, sizeof(a)) || !take_word(cursor, b, sizeof(b))) {
            emit("usage: import <listfile> <diskfile>\r\n");
            return 1;
        }
        if (fl_fs_ramfs_cat(session, a, c, sizeof(c)) != 0) {
            emit("not found\r\n");
            return 1;
        }
        if (fl_fs_labdisk_put(b, c) == 0)
            emit("import ok\r\n");
        else
            emit("import failed\r\n");
        return 1;
    }
    return 0;
}

static void lookup_host(const char *host)
{
    fl_fs_labnet_nslookup(host, emit);
}

static int run_net(const char *verb, const char **cursor)
{
    char a[64];
    char b[40];
    char mac[20];
    if (str_eq(verb, "ifconfig")) {
        fl_fs_labnet_ifconfig(emit, emit_uint);
        return 1;
    }
    if (str_eq(verb, "route")) {
        fl_fs_labnet_route(emit);
        return 1;
    }
    if (str_eq(verb, "netstat")) {
        fl_fs_labnet_netstat(emit, emit_uint);
        return 1;
    }
    if (str_eq(verb, "arp")) {
        if (!take_word(cursor, a, sizeof(a)))
            a[0] = 0;
        take_word(cursor, b, sizeof(b));
        take_word(cursor, mac, sizeof(mac));
        skip_spaces(cursor);
        fl_fs_labnet_arp(a[0] ? a : 0, b[0] ? b : 0, **cursor ? *cursor : (mac[0] ? mac : 0), emit, emit_uint);
        return 1;
    }
    if (str_eq(verb, "nslookup") || str_eq(verb, "resolve")) {
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: nslookup <host>\r\n");
            return 1;
        }
        lookup_host(a);
        return 1;
    }
    if (str_eq(verb, "dnsack")) {
        char v6[40];
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: dnsack <host> <ip> [ipv6]\r\n");
            return 1;
        }
        if (!take_word(cursor, b, sizeof(b)))
            str_copy(b, "fail", sizeof(b));
        if (!take_word(cursor, v6, sizeof(v6)))
            v6[0] = 0;
        (void)fl_fs_labnet_dnsack(a, b, v6[0] ? v6 : 0, emit, emit_uint);
        return 1;
    }
    if (str_eq(verb, "ping") || str_eq(verb, "ping6")) {
        unsigned port = 0;
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: ping <host> [port]\r\n");
            return 1;
        }
        if (take_word(cursor, b, sizeof(b)))
            port = parse_u(b);
        (void)fl_fs_labnet_ping(a, port, str_eq(verb, "ping6"), emit, emit_uint);
        return 1;
    }
    if (str_eq(verb, "check")) {
        if (!take_word(cursor, a, sizeof(a))) {
            emit("check requirements <host> <port>\r\n");
            return 1;
        }
        if (!str_eq(a, "requirements")) {
            emit("check requirements <host> <port>\r\n");
            return 1;
        }
        if (!take_word(cursor, a, sizeof(a))) {
            emit("check requirements <host> <port>\r\n");
            return 1;
        }
        if (!take_word(cursor, b, sizeof(b)))
            str_copy(b, "0", sizeof(b));
        (void)fl_fs_labnet_check(a, parse_u(b), emit, emit_uint);
        return 1;
    }
    if (str_eq(verb, "udpsend")) {
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: udpsend <ip:port> <msg>\r\n");
            return 1;
        }
        skip_spaces(cursor);
        if (!**cursor) {
            emit("usage: udpsend <ip:port> <msg>\r\n");
            return 1;
        }
        if (fl_fs_labnet_udpsend(a, *cursor) == 0) {
            emit("SERVER_RELAY udp ");
            emit(a);
            emit(" ");
            emit(*cursor);
            emit("\r\n");
            emit("udpsend ok\r\n");
        } else {
            emit("udpsend failed\r\n");
        }
        return 1;
    }
    if (str_eq(verb, "udplisten")) {
        char msg[48];
        if (!take_word(cursor, a, sizeof(a))) {
            emit("usage: udplisten <port>\r\n");
            return 1;
        }
        if (fl_fs_labnet_udplisten(parse_u(a), msg, sizeof(msg)) != 0) {
            emit("udplisten failed\r\n");
            return 1;
        }
        emit("udplisten :");
        emit(a);
        if (msg[0]) {
            emit(" ");
            emit(msg);
        }
        emit("\r\n");
        return 1;
    }
    if (str_eq(verb, "wifi")) {
        if (!take_word(cursor, a, sizeof(a)))
            a[0] = 0;
        skip_spaces(cursor);
        fl_fs_labnet_wifi(a[0] ? a : "scan", *cursor, emit);
        return 1;
    }
    if (str_eq(verb, "netsh")) {
        if (!take_word(cursor, a, sizeof(a))) {
            emit("netsh arp|ifconfig|route|netstat|nslookup|wifi|ping\r\n");
            return 1;
        }
        return run_net(a, cursor);
    }
    return 0;
}

/* netlab helpers defined further below; run_server reacts to server lifecycle. */
static void nl_pick_host(void);
static void nl_pod_dissolve(void);
static void nl_display_name(int session, char *out, unsigned cap);

static int run_server(int session, const char *verb, const char **cursor)
{
    char sub[16];
    if (!str_eq(verb, "server"))
        return 0;
    if (!take_word(cursor, sub, sizeof(sub))) {
        emit("server host|join|leave|kill|msg|announce|connected|interfaces|nick|file\r\n");
        return 1;
    }
    if (str_eq(sub, "interfaces")) {
        fl_fs_labnet_ifconfig(emit, emit_uint);
        return 1;
    }
    if (s_lab_netmode) {
        /* Real network dropped for an active netlab session: refuse to reach
         * the relay so nothing leaves while the MitM+RS demo runs. */
        emit("server paused: lab isolated (netlab MitM+RS active) — run `netlab stop` to resume the real network\r\n");
        return 1;
    }
    /* Best-effort local signal for netlab's pod/host-collision check (see
     * s_nl_acting_host_session): record that THIS session forwarded a host
     * request, and clear it when this session leaves/kills its own server. */
    if (str_eq(sub, "host")) {
        s_nl_acting_host_session = session;
    } else if (str_eq(sub, "kill")) {
        if (s_nl_acting_host_session == session) {
            s_nl_acting_host_session = -1;
            /* The server host tore the whole server down: the pods that live
             * under it are killed too. */
            if (s_nl_pod[0]) {
                nl_pod_dissolve();
                emit("netlab: server killed by its host — pod dissolved.\r\n");
            }
        }
    } else if (str_eq(sub, "leave")) {
        if (s_nl_acting_host_session == session) {
            s_nl_acting_host_session = -1;
            /* Host stepped down (the relay picks a new server host by its own
             * join order); re-elect the pod host independently by pod join
             * order, so the two new hosts may differ. */
            if (s_nl_pod[0]) {
                int before = s_nl_hostidx;
                nl_pick_host();
                if (s_nl_hostidx != before && s_nl_hostidx >= 0) {
                    char dn2[40];
                    nl_display_name(s_nl_members[s_nl_hostidx], dn2, sizeof(dn2));
                    emit("netlab: server host stepped down — new pod host: ");
                    emit(dn2); emit("\r\n");
                }
            }
        }
    }
    skip_spaces(cursor);
    emit("SERVER_RELAY ");
    emit(sub);
    if (**cursor) {
        emit(" ");
        emit(*cursor);
    }
    emit("\r\n");
    return 1;
}

/* ------------------------------------------------------------------------- */
/* netlab — contained network-security teaching lab (freestanding analog of   */
/* the hosted `netlab`). Simulation only: fabricated pods, no sockets, no real */
/* interface, no interception of anyone, no terminal injection, no capture     */
/* forgery. The one new power here is the lab/live network switch: `start`     */
/* drops the real network (relay) for the demo, `stop` resumes it.             */
/* ------------------------------------------------------------------------- */

struct nl_host {
    const char *name;
    const char *ip;
    const char *mac;
    const char *role;
};

struct nl_pod {
    const char *name;
    const char *ssid;
    const struct nl_host *hosts;
    unsigned host_count;
    /* roles pre-resolved for the walkthrough and capture summary */
    const char *gw_ip;
    const char *gw_mac;
    const char *obs_mac;
    const char *st_name;
    const char *st_ip;
    const char *subnet;  /* e.g. "192.168.56.0/24" for the scan header   */
    const char *ap_sec;  /* simulated AP key-mgmt                         */
    const char *ap_psk;  /* FABRICATED lab-only pre-shared key            */
};

static const struct nl_host NL_POD1_HOSTS[] = {
    { "gateway",  "192.168.56.1",  "02:00:00:00:56:01", "gateway"  },
    { "alice",    "192.168.56.50", "02:00:00:00:56:32", "station"  },
    { "bob",      "192.168.56.60", "02:00:00:00:56:3c", "station"  },
    { "observer", "192.168.56.76", "02:00:00:00:56:4c", "observer" },
};

static const struct nl_host NL_POD2_HOSTS[] = {
    { "gateway", "10.10.0.1",  "02:00:00:00:0a:01", "gateway"  },
    { "carol",   "10.10.0.20", "02:00:00:00:0a:14", "station"  },
    { "ids",     "10.10.0.9",  "02:00:00:00:0a:09", "observer" },
};

static const struct nl_pod NL_PODS[] = {
    { "redteam-lab",  "FLINT-LAB-AP",  NL_POD1_HOSTS, 4,
      "192.168.56.1", "02:00:00:00:56:01", "02:00:00:00:56:4c", "alice", "192.168.56.50",
      "192.168.56.0/24", "WPA2-PSK (CCMP)", "flint-lab-RED-9t4w-demo" },
    { "blueteam-lab", "FLINT-BLUE-AP", NL_POD2_HOSTS, 3,
      "10.10.0.1", "02:00:00:00:0a:01", "02:00:00:00:0a:09", "carol", "10.10.0.20",
      "10.10.0.0/24", "WPA3-SAE", "flint-lab-BLUE-2k8p-demo" },
};
#define NL_POD_COUNT (sizeof(NL_PODS) / sizeof(NL_PODS[0]))

static const struct nl_pod *nl_find_pod(const char *name)
{
    unsigned i;
    if (!name || !name[0])
        return 0;
    for (i = 0; i < NL_POD_COUNT; ++i) {
        if (str_eq(name, NL_PODS[i].name))
            return &NL_PODS[i];
    }
    /* accept "1"/"2" as pod ids too */
    if (str_eq(name, "1"))
        return &NL_PODS[0];
    if (str_eq(name, "2"))
        return &NL_PODS[1];
    return 0;
}

static void nl_boundary(void)
{
    emit("  [simulation] fabricated topology in memory only; no real network,\r\n");
    emit("  [simulation] no interception of anyone, no terminal injection.\r\n");
}

static void nl_usage(void)
{
    emit("netlab — contained net-security teaching lab (simulation only)\r\n");
    emit("  netlab consent | pods | defend | rs-explain\r\n");
    emit("  netlab hosts <pod> | scan <pod> | wpa <pod> | topology <pod>\r\n");
    emit("  netlab traceroute <pod> <dst> | monitor <pod> | arp-demo <pod> | capture <pod>\r\n");
    emit("  netlab pod create <pod> | join | members | host | leave   (your real account)\r\n");
    emit("  netlab listen | listen cancel      (open/cancel a listener as the attacker)\r\n");
    emit("  netlab accept                      (bind as the accepting side; victim-side consent)\r\n");
    emit("  netlab <cmd>                       (attacker only: runs in the isolated sandbox)\r\n");
    emit("  netlab end | status\r\n");
    emit("  netlab start | stop   (lab-isolate: drop/resume real network)\r\n");
}

static void nl_set_mode(int lab)
{
    s_lab_netmode = lab ? 1 : 0;
    /* Control token for tools/browser-lab/lab.js to drop/resume the relay. */
    emit(lab ? "LAB_NETMODE lab\r\n" : "LAB_NETMODE live\r\n");
    if (lab) {
        emit("netlab: lab-isolated — real network (server relay) dropped for the demo.\r\n");
        nl_boundary();
    } else {
        emit("netlab: live — real network restored; server relay may resume.\r\n");
    }
}

static int nl_session_idx(int session)
{
    int i;
    for (i = 0; i < s_nl_memcount; ++i)
        if (s_nl_members[i] == session)
            return i;
    return -1;
}

/* Reassign the pod host: the first member (by join order) that is not the
 * locally-tracked real server host. Leaves s_nl_hostidx at -1 ("pending") if
 * every current member is that acting host. */
static void nl_pick_host(void)
{
    int i;
    s_nl_hostidx = -1;
    for (i = 0; i < s_nl_memcount; ++i) {
        if (s_nl_members[i] == s_nl_acting_host_session)
            continue;
        s_nl_hostidx = i;
        break;
    }
}

/* Real account name for `session`, with a " {N}" suffix when an earlier pod
 * member (by join order) is logged into the same account — e.g. a second
 * concurrent session logged in as "flinstone" displays as "flinstone {2}". */
static void nl_display_name(int session, char *out, unsigned cap)
{
    const char *base = fl_fs_identity_user(session);
    int idx = nl_session_idx(session);
    int dup = 1, i;
    unsigned n;
    if (idx >= 0) {
        for (i = 0; i < idx; ++i)
            if (str_eq(fl_fs_identity_user(s_nl_members[i]), base))
                ++dup;
    }
    if (dup <= 1) {
        str_copy(out, base, cap);
        return;
    }
    n = 0;
    while (base[n] && n + 1 < cap) { out[n] = base[n]; ++n; }
    if (n + 5 < cap) {
        out[n++] = ' '; out[n++] = '{';
        if (dup >= 10)
            out[n++] = (char)('0' + (dup / 10) % 10);
        out[n++] = (char)('0' + dup % 10);
        out[n++] = '}';
    }
    out[n] = 0;
}

static int nl_is_mgmt(const char *s)
{
    static const char *m[] = {
        "help", "consent", "pods", "status", "start", "stop", "rs-explain",
        "defend", "hosts", "topology", "monitor", "arp-demo", "capture",
        "traceroute", "scan", "wpa", "pod", "listen", "accept", "end", 0
    };
    int i;
    for (i = 0; m[i]; ++i)
        if (str_eq(s, m[i]))
            return 1;
    return 0;
}

static void nl_sb_reset(void)
{
    int i;
    for (i = 0; i < NL_SB_FILES; ++i)
        s_nl_sb_used[i] = 0;
    s_nl_sb_count = 0;
}

/* Tear down all pod + reverse-shell state and restore the real network. Used
 * when the server host kills the server (the pods under it die too). */
static void nl_pod_dissolve(void)
{
    s_nl_pod[0] = 0;
    s_nl_memcount = 0;
    s_nl_hostidx = -1;
    s_nl_listen_session = -1;
    s_nl_atk_session = -1;
    s_nl_victim_session = -1;
    nl_sb_reset();
    if (s_lab_netmode)
        nl_set_mode(0);
}

static int nl_sb_find(const char *name)
{
    int i;
    for (i = 0; i < NL_SB_FILES; ++i)
        if (s_nl_sb_used[i] && str_eq(s_nl_sb_name[i], name))
            return i;
    return -1;
}

/* Real execution against the disposable sandbox's own isolated state — not
 * canned text, but never the accepting session's real files either. */
static void nl_sb_run(const char *cmd, const char **cursor)
{
    char vdn[40];
    nl_display_name(s_nl_victim_session, vdn, sizeof(vdn));
    if (str_eq(cmd, "whoami")) { emit(vdn); emit("\r\n"); return; }
    if (str_eq(cmd, "hostname")) { emit(vdn); emit("-sandbox\r\n"); return; }
    if (str_eq(cmd, "id")) {
        emit("uid=1000("); emit(vdn); emit(") gid=1000("); emit(vdn); emit(")\r\n");
        return;
    }
    if (str_eq(cmd, "uname")) { emit("FlinstoneLab x86_64 (isolated sandbox)\r\n"); return; }
    if (str_eq(cmd, "pwd")) { emit("/sandbox/"); emit(vdn); emit("\r\n"); return; }
    if (str_eq(cmd, "ifconfig")) {
        emit("eth0  inet 192.168.56.76  ether 02:00:00:00:56:4c  (simulated)\r\n");
        return;
    }
    if (str_eq(cmd, "netstat")) {
        emit("tcp 192.168.56.76:4444 ESTABLISHED (simulated reverse-shell socket)\r\n");
        return;
    }
    if (str_eq(cmd, "ls")) {
        int i, any = 0;
        for (i = 0; i < NL_SB_FILES; ++i) {
            if (!s_nl_sb_used[i])
                continue;
            emit(s_nl_sb_name[i]);
            emit("  ");
            any = 1;
        }
        if (!any)
            emit("(empty sandbox)");
        emit("\r\n");
        return;
    }
    if (str_eq(cmd, "write")) {
        char fname[NL_SB_NAMELEN];
        int idx, slot;
        if (!take_word(cursor, fname, sizeof(fname))) {
            emit("usage: write <file> <content>\r\n");
            return;
        }
        skip_spaces(cursor);
        idx = nl_sb_find(fname);
        if (idx < 0) {
            for (slot = 0; slot < NL_SB_FILES && s_nl_sb_used[slot]; ++slot) { }
            if (slot >= NL_SB_FILES) { emit("sandbox full\r\n"); return; }
            str_copy(s_nl_sb_name[slot], fname, NL_SB_NAMELEN);
            s_nl_sb_used[slot] = 1;
            if (slot >= s_nl_sb_count)
                s_nl_sb_count = slot + 1;
            idx = slot;
        }
        str_copy(s_nl_sb_content[idx], *cursor, NL_SB_CONTENTLEN);
        emit("wrote "); emit(fname); emit(" (sandbox)\r\n");
        return;
    }
    if (str_eq(cmd, "cat")) {
        char fname[NL_SB_NAMELEN];
        int idx;
        if (!take_word(cursor, fname, sizeof(fname))) { emit("usage: cat <file>\r\n"); return; }
        idx = nl_sb_find(fname);
        if (idx < 0) { emit("cat: "); emit(fname); emit(": not found\r\n"); return; }
        emit(s_nl_sb_content[idx]); emit("\r\n");
        return;
    }
    if (str_eq(cmd, "rm")) {
        char fname[NL_SB_NAMELEN];
        int idx;
        if (!take_word(cursor, fname, sizeof(fname))) { emit("usage: rm <file>\r\n"); return; }
        idx = nl_sb_find(fname);
        if (idx < 0) { emit("rm: "); emit(fname); emit(": not found\r\n"); return; }
        s_nl_sb_used[idx] = 0;
        emit("removed "); emit(fname); emit(" (sandbox)\r\n");
        return;
    }
    emit(cmd); emit(": ran in the isolated sandbox (output not modeled)\r\n");
}

static int run_netlab(int session, const char *verb, const char **cursor)
{
    char sub[24];
    char pod[24];
    char dst[24];
    char nm[24];
    const struct nl_pod *p;
    unsigned i;

    if (!str_eq(verb, "netlab"))
        return 0;

    if (!take_word(cursor, sub, sizeof(sub))) {
        nl_usage();
        return 1;
    }

    if (str_eq(sub, "help")) {
        nl_usage();
        return 1;
    }
    if (str_eq(sub, "consent")) {
        emit("netlab runs on a FABRICATED topology inside this lab only.\r\n");
        emit("It does not touch a real network, intercept anyone, inject into a\r\n");
        emit("terminal, or forge a capture. `netlab start` drops the real network\r\n");
        emit("(the server relay) for the demo; `netlab stop` resumes it.\r\n");
        return 1;
    }
    if (str_eq(sub, "pods")) {
        emit("simulated pods:\r\n");
        for (i = 0; i < NL_POD_COUNT; ++i) {
            emit("  ");
            emit(NL_PODS[i].name);
            emit("  ssid=");
            emit(NL_PODS[i].ssid);
            emit("  hosts=");
            emit_uint(NL_PODS[i].host_count);
            emit("  consent=all-agreed\r\n");
        }
        return 1;
    }
    if (str_eq(sub, "status")) {
        emit("netlab status:\r\n");
        emit("  net: ");
        emit(s_lab_netmode ? "lab-isolated (real network dropped)\r\n"
                           : "live (real network available)\r\n");
        emit("  pod: ");
        if (s_nl_pod[0]) {
            emit(s_nl_pod);
            if (s_nl_hostidx >= 0) {
                char dn[40];
                nl_display_name(s_nl_members[s_nl_hostidx], dn, sizeof(dn));
                emit("  host="); emit(dn);
            } else {
                emit("  host=(pending)");
            }
            emit("\r\n");
        } else {
            emit("(none)\r\n");
        }
        emit("  listener: ");
        if (s_nl_listen_session >= 0) {
            char dn[40];
            nl_display_name(s_nl_listen_session, dn, sizeof(dn));
            emit(dn); emit(" (open)\r\n");
        } else {
            emit("(none)\r\n");
        }
        emit("  reverse shell: ");
        if (s_nl_atk_session >= 0) {
            char adn[40], vdn[40];
            nl_display_name(s_nl_atk_session, adn, sizeof(adn));
            nl_display_name(s_nl_victim_session, vdn, sizeof(vdn));
            emit("OPEN  "); emit(adn); emit(" -> sandbox("); emit(vdn); emit(") [simulated]\r\n");
        } else {
            emit("closed\r\n");
        }
        return 1;
    }
    if (str_eq(sub, "start")) {
        nl_set_mode(1);
        return 1;
    }
    if (str_eq(sub, "stop")) {
        nl_set_mode(0);
        return 1;
    }
    if (str_eq(sub, "pod")) {
        if (!take_word(cursor, sub, sizeof(sub))) {
            emit("usage: netlab pod create|join|members|host|leave\r\n");
            return 1;
        }
        if (str_eq(sub, "create")) {
            if (!take_word(cursor, pod, sizeof(pod))) {
                emit("usage: netlab pod create <pod>\r\n");
                return 1;
            }
            str_copy(s_nl_pod, pod, NL_NAMELEN);
            s_nl_memcount = 0;
            s_nl_members[s_nl_memcount++] = session;
            nl_pick_host();
            emit("netlab: pod '");
            emit(s_nl_pod);
            emit("' created.\r\n");
            if (s_nl_hostidx >= 0) {
                char dn[40];
                nl_display_name(s_nl_members[s_nl_hostidx], dn, sizeof(dn));
                emit("  host is "); emit(dn); emit(" (simulated, lab networks by default)\r\n");
            } else {
                emit("  host pending: you are currently the real server host, so this pod\r\n");
                emit("  has no pod host yet. Have another real session join, or `server kill`\r\n");
                emit("  your own real hosting, to free up a pod host.\r\n");
            }
            return 1;
        }
        if (str_eq(sub, "join")) {
            if (!s_nl_pod[0]) {
                emit("netlab: no pod yet. `netlab pod create <pod>` first.\r\n");
                return 1;
            }
            if (nl_session_idx(session) >= 0) {
                emit("netlab: you are already in pod ");
                emit(s_nl_pod);
                emit(".\r\n");
                return 1;
            }
            if (s_nl_memcount >= NL_MAXMEM) {
                emit("netlab: pod is full.\r\n");
                return 1;
            }
            s_nl_members[s_nl_memcount++] = session;
            nl_pick_host();
            {
                char dn[40];
                nl_display_name(session, dn, sizeof(dn));
                emit("netlab: "); emit(dn); emit(" joined pod "); emit(s_nl_pod);
                emit(" (identity preserved).\r\n");
            }
            if (s_nl_hostidx >= 0) {
                char dn2[40];
                nl_display_name(s_nl_members[s_nl_hostidx], dn2, sizeof(dn2));
                emit("  host: "); emit(dn2); emit("\r\n");
            } else {
                emit("  host pending: every current member is the real server host.\r\n");
            }
            return 1;
        }
        if (str_eq(sub, "members")) {
            if (!s_nl_pod[0]) {
                emit("netlab: no pod.\r\n");
                return 1;
            }
            emit("pod ");
            emit(s_nl_pod);
            emit(" members:\r\n");
            for (i = 0; i < (unsigned)s_nl_memcount; ++i) {
                char dn[40];
                nl_display_name(s_nl_members[i], dn, sizeof(dn));
                emit("  "); emit(dn);
                if ((int)i == s_nl_hostidx)
                    emit("  (host)");
                if (s_nl_members[i] == s_nl_acting_host_session)
                    emit("  (real server host)");
                emit("\r\n");
            }
            return 1;
        }
        if (str_eq(sub, "host")) {
            if (!s_nl_pod[0]) {
                emit("netlab: no pod.\r\n");
                return 1;
            }
            emit("pod ");
            emit(s_nl_pod);
            emit(" host: ");
            if (s_nl_hostidx >= 0) {
                char dn[40];
                nl_display_name(s_nl_members[s_nl_hostidx], dn, sizeof(dn));
                emit(dn); emit("\r\n");
            } else {
                emit("(none - pending)\r\n");
            }
            return 1;
        }
        if (str_eq(sub, "leave")) {
            int idx, was_host, j;
            char dn[40];
            if (!s_nl_pod[0]) {
                emit("netlab: not in a pod.\r\n");
                return 1;
            }
            idx = nl_session_idx(session);
            if (idx < 0) {
                emit("netlab: you are not in this pod.\r\n");
                return 1;
            }
            was_host = ((int)idx == s_nl_hostidx);
            nl_display_name(session, dn, sizeof(dn));
            for (j = idx; j < s_nl_memcount - 1; ++j)
                s_nl_members[j] = s_nl_members[j + 1];
            s_nl_memcount--;
            if (s_nl_memcount == 0) {
                emit("netlab: "); emit(dn); emit(" left; pod dissolved.\r\n");
                s_nl_pod[0] = 0;
                s_nl_hostidx = -1;
                s_nl_listen_session = -1;
                s_nl_atk_session = -1;
                s_nl_victim_session = -1;
                nl_sb_reset();
                if (s_lab_netmode) nl_set_mode(0);  /* never leave the net paused */
                return 1;
            }
            nl_pick_host();
            emit("netlab: "); emit(dn); emit(" left.\r\n");
            if (s_nl_hostidx >= 0) {
                char dn2[40];
                nl_display_name(s_nl_members[s_nl_hostidx], dn2, sizeof(dn2));
                emit("  host "); emit(was_host ? "succession: " : "remains: ");
                emit(dn2); emit("\r\n");
            } else {
                emit("  host pending: every remaining member is the real server host.\r\n");
            }
            if (session == s_nl_atk_session || session == s_nl_victim_session) {
                s_nl_atk_session = -1;
                s_nl_victim_session = -1;
                nl_sb_reset();
                nl_set_mode(0);
                emit("netlab: your reverse-shell binding ended because you left the pod.\r\n");
            }
            if (session == s_nl_listen_session)
                s_nl_listen_session = -1;
            return 1;
        }
        emit("usage: netlab pod create|join|members|host|leave\r\n");
        return 1;
    }
    if (str_eq(sub, "listen")) {
        if (!s_nl_pod[0] || nl_session_idx(session) < 0) {
            emit("netlab: join a pod first (`netlab pod create` or `netlab pod join`).\r\n");
            return 1;
        }
        if (take_word(cursor, nm, sizeof(nm)) && str_eq(nm, "cancel")) {
            if (s_nl_listen_session != session) {
                emit("netlab: you have no open listener to cancel.\r\n");
                return 1;
            }
            s_nl_listen_session = -1;
            emit("netlab: listener cancelled.\r\n");
            return 1;
        }
        if (s_nl_atk_session >= 0) {
            emit("netlab: a reverse shell is already bound in this pod; `netlab end` it first.\r\n");
            return 1;
        }
        if (s_nl_listen_session >= 0) {
            emit("netlab: a listener is already open in this pod. `netlab accept` it, or its\r\n");
            emit("  owner can `netlab listen cancel`.\r\n");
            return 1;
        }
        s_nl_listen_session = session;
        {
            char dn[40];
            nl_display_name(session, dn, sizeof(dn));
            emit("netlab: "); emit(dn); emit(" is listening in pod "); emit(s_nl_pod);
            emit(".\r\n  Another pod member can `netlab accept` to bind as the victim side\r\n");
            emit("  (SIMULATED — a disposable isolated sandbox, never their real session).\r\n");
        }
        return 1;
    }
    if (str_eq(sub, "accept")) {
        if (!s_nl_pod[0] || nl_session_idx(session) < 0) {
            emit("netlab: join a pod first.\r\n");
            return 1;
        }
        if (s_nl_listen_session < 0) {
            emit("netlab: no open listener in this pod. Someone runs `netlab listen` first.\r\n");
            return 1;
        }
        if (s_nl_listen_session == session) {
            emit("netlab: you cannot accept your own listener.\r\n");
            return 1;
        }
        if (s_nl_atk_session >= 0) {
            emit("netlab: already bound; `netlab end` it first.\r\n");
            return 1;
        }
        /* "when we switch to lab network": re-resolve the pod host here, since
         * this is the actual isolation moment and the real server host may
         * have changed since the pod was created or last joined. */
        nl_pick_host();
        s_nl_atk_session = s_nl_listen_session;
        s_nl_victim_session = session;
        s_nl_listen_session = -1;
        nl_sb_reset();
        nl_set_mode(1);
        {
            char adn[40], vdn[40];
            nl_display_name(s_nl_atk_session, adn, sizeof(adn));
            nl_display_name(s_nl_victim_session, vdn, sizeof(vdn));
            emit("netlab: "); emit(vdn); emit(" accepted. "); emit(adn);
            emit(" is bound as attacker against a fresh, isolated SANDBOX\r\n");
            emit("  labeled for "); emit(vdn); emit(" — not "); emit(vdn);
            emit("'s real session or real files.\r\n  ");
            emit(adn);
            emit(": run `netlab <cmd>` to execute in the sandbox. Either side can\r\n");
            emit("  `netlab end` it at any time.\r\n");
        }
        return 1;
    }
    if (str_eq(sub, "end")) {
        if (session != s_nl_atk_session && session != s_nl_victim_session) {
            emit("netlab: no reverse shell bound to you.\r\n");
            return 1;
        }
        {
            char adn[40], vdn[40];
            nl_display_name(s_nl_atk_session, adn, sizeof(adn));
            nl_display_name(s_nl_victim_session, vdn, sizeof(vdn));
            emit("netlab: reverse shell between "); emit(adn); emit(" and "); emit(vdn);
            emit(" closed.\r\n");
        }
        s_nl_atk_session = -1;
        s_nl_victim_session = -1;
        nl_sb_reset();
        nl_set_mode(0);
        return 1;
    }
    if (str_eq(sub, "rs-explain")) {
        emit("reverse shell: the target dials OUT to a listener and wires its\r\n");
        emit("shell to that socket, so inbound firewalls are bypassed. netlab does\r\n");
        emit("NOT implement one (that is remote control of a victim). Defenders\r\n");
        emit("watch outbound to odd ports (4444), shells that own sockets, and use\r\n");
        emit("default-deny egress.\r\n");
        return 1;
    }
    if (str_eq(sub, "defend")) {
        emit("defend — detection & prevention:\r\n");
        emit("  ARP/MiTM: dynamic ARP inspection, static gateway ARP, alert on two\r\n");
        emit("    MACs claiming one IP, encrypt+authenticate, WPA2/WPA3, segment.\r\n");
        emit("  reverse shell: default-deny egress, watch odd outbound ports,\r\n");
        emit("    EDR/IDS for listener/beacon patterns, key-based SSH, fail2ban.\r\n");
        emit("  DNS: DNSSEC; capture and review traffic routinely.\r\n");
        return 1;
    }

    /* verbs that take a pod */
    if (str_eq(sub, "hosts") || str_eq(sub, "topology") ||
        str_eq(sub, "monitor") || str_eq(sub, "arp-demo") ||
        str_eq(sub, "capture") || str_eq(sub, "traceroute") ||
        str_eq(sub, "scan") || str_eq(sub, "wpa")) {
        if (!take_word(cursor, pod, sizeof(pod))) {
            emit("usage: netlab ");
            emit(sub);
            emit(" <pod>  (try `netlab pods`)\r\n");
            return 1;
        }
        p = nl_find_pod(pod);
        if (!p) {
            emit("netlab: no pod '");
            emit(pod);
            emit("' (try `netlab pods`)\r\n");
            return 1;
        }
        if (str_eq(sub, "scan")) {
            emit("netlab scan (simulated host discovery) pod ");
            emit(p->name);
            emit(" — ");
            emit(p->subnet);
            emit("\r\n");
            for (i = 0; i < p->host_count; ++i) {
                emit("  host up   ");
                emit(p->hosts[i].ip);
                emit("  MAC ");
                emit(p->hosts[i].mac);
                emit("  (");
                emit(p->hosts[i].role);
                emit(" ");
                emit(p->hosts[i].name);
                emit(")\r\n");
            }
            emit("  hosts up (simulated). No real network was scanned.\r\n");
            return 1;
        }
        if (str_eq(sub, "wpa")) {
            emit("netlab wpa (simulated AP credentials) pod ");
            emit(p->name);
            emit("\r\n  ssid:     ");
            emit(p->ssid);
            emit("\r\n  key-mgmt: ");
            emit(p->ap_sec);
            emit("\r\n  psk:      ");
            emit(p->ap_psk);
            emit("\r\n  note: fabricated lab value. netlab reads no real saved\r\n");
            emit("        networks and cracks no key. `netlab defend` for fixes.\r\n");
            return 1;
        }
        if (str_eq(sub, "hosts")) {
            emit("pod ");
            emit(p->name);
            emit(" ssid=");
            emit(p->ssid);
            emit("\r\n");
            for (i = 0; i < p->host_count; ++i) {
                emit("  ");
                emit(p->hosts[i].name);
                emit("  ");
                emit(p->hosts[i].ip);
                emit("  ");
                emit(p->hosts[i].mac);
                emit("  ");
                emit(p->hosts[i].role);
                emit("\r\n");
            }
            return 1;
        }
        if (str_eq(sub, "topology")) {
            emit("pod ");
            emit(p->name);
            emit(" (ssid ");
            emit(p->ssid);
            emit(")\r\n");
            emit("   [gateway ");
            emit(p->gw_ip);
            emit("]\r\n        |\r\n");
            for (i = 0; i < p->host_count; ++i) {
                if (str_eq(p->hosts[i].role, "gateway"))
                    continue;
                emit("   +-- ");
                emit(p->hosts[i].name);
                emit(" ");
                emit(p->hosts[i].ip);
                if (str_eq(p->hosts[i].role, "observer"))
                    emit("  (observer vantage)");
                emit("\r\n");
            }
            return 1;
        }
        if (str_eq(sub, "traceroute")) {
            const struct nl_host *h = 0;
            if (!take_word(cursor, dst, sizeof(dst))) {
                emit("usage: netlab traceroute <pod> <dst>\r\n");
                return 1;
            }
            for (i = 0; i < p->host_count; ++i) {
                if (str_eq(dst, p->hosts[i].name)) {
                    h = &p->hosts[i];
                    break;
                }
            }
            if (!h) {
                emit("netlab: no host '");
                emit(dst);
                emit("' in pod ");
                emit(p->name);
                emit("\r\n");
                return 1;
            }
            emit("traceroute (simulated) to ");
            emit(h->name);
            emit(":\r\n");
            if (!str_eq(h->role, "gateway")) {
                emit("  1  gateway   ");
                emit(p->gw_ip);
                emit("\r\n  2  ");
                emit(h->name);
                emit("   ");
                emit(h->ip);
                emit("\r\n");
            } else {
                emit("  1  gateway   ");
                emit(p->gw_ip);
                emit("\r\n");
            }
            return 1;
        }
        /* monitor / capture / arp-demo share the scripted-frame story */
        emit(str_eq(sub, "capture") ? "capture (simulated frames) pod " :
             str_eq(sub, "arp-demo") ? "arp-demo (how ARP-spoof MiTM works) pod " :
                                       "monitor (simulated) pod ");
        emit(p->name);
        emit(":\r\n");
        emit("  #1 ARP  who-has ");
        emit(p->gw_ip);
        emit("? (request, ");
        emit(p->st_name);
        emit("->broadcast)\r\n");
        emit("  #2 ARP  ");
        emit(p->gw_ip);
        emit(" is-at ");
        emit(p->gw_mac);
        emit(" (truthful reply)\r\n");
        emit("  #3 ARP  ");
        emit(p->gw_ip);
        emit(" is-at ");
        emit(p->obs_mac);
        emit("  <-- SPOOF (observer claims gateway)\r\n");
        emit("  #4 ARP  ");
        emit(p->st_ip);
        emit(" is-at ");
        emit(p->obs_mac);
        emit("  <-- SPOOF (observer claims station to gateway)\r\n");
        emit("  #5 UDP  ");
        emit(p->st_ip);
        emit("->");
        emit(p->gw_ip);
        emit(" over the poisoned path\r\n");
        if (str_eq(sub, "capture")) {
            emit("  [ALERT] ARP conflict: ");
            emit(p->gw_ip);
            emit(" claimed by ");
            emit(p->gw_mac);
            emit(" and ");
            emit(p->obs_mac);
            emit(" (ARP-spoofing signature)\r\n");
            emit("  verdict: duplicate IP->MAC binding — consistent with a MiTM capture.\r\n");
        }
        if (str_eq(sub, "arp-demo")) {
            emit("  detection: two MACs claiming ");
            emit(p->gw_ip);
            emit(" is the giveaway — `netlab capture ");
            emit(p->name);
            emit("` raises the alert; `netlab defend` lists fixes.\r\n");
        }
        return 1;
    }

    /* reverse-shell redirection: only the BOUND ATTACKER session's
     * non-management verbs run against the sandbox. Any other session,
     * including the accepting victim typing in their own terminal, is
     * completely unaffected — their real shell behaves normally. */
    if (session == s_nl_atk_session && !nl_is_mgmt(sub)) {
        nl_sb_run(sub, cursor);
        return 1;
    }

    emit("netlab: unknown verb '");
    emit(sub);
    emit("' (try `netlab help`)\r\n");
    return 1;
}

static int run_meta(int session, const char *verb, const char **cursor)
{
    char arg[24];
    if (str_eq(verb, "help")) {
        emit("whoami users login logout su sudo useradd userdel passwd switchuser session\r\n");
        emit("dir ls pwd cd cat type write make mkdir rmdir rmtree rm mv du search listdirs loc where\r\n");
        emit("createdisk format initdisk setdisk printdisk listclusters writecluster delcluster addcluster update import\r\n");
        emit("diskput diskget diskfiles diskmkdir diskdel\r\n");
        emit("ping ping6 check ifconfig arp route netstat nslookup resolve netsh wifi udpsend udplisten\r\n");
        emit("netlab (pods|hosts|scan|wpa|topology|traceroute|monitor|arp-demo|capture|start|stop|status|defend)\r\n");
        emit("netlab pod create|join|members|host|leave (real account) | listen|accept|end  (`netlab help` for all)\r\n");
        emit("server version contracts audit redirect rerun help history his cc clear exit bios\r\n");
        emit("lab analogs: ramfs, cluster disk, lab net, P3 relay. Hosted FAT32/sockets stay on the ELF shell.\r\n");
        return 1;
    }
    if (str_eq(verb, "whoami")) {
        emit("WHOAMI ");
        emit(fl_fs_identity_user(session));
        emit(fl_fs_identity_elevated(session) ? " elevated\r\n" : "\r\n");
        return 1;
    }
    if (str_eq(verb, "version")) {
        emit("4.5.2 lab\r\n");
        return 1;
    }
    if (str_eq(verb, "contracts")) {
        take_word(cursor, arg, sizeof(arg));
        if (str_eq(arg, "json"))
            emit("{\"lab\":[\"identity\",\"ramfs\",\"labdisk\",\"labnet\",\"server\"],\"p8_browser_artifact_rev\":4}\r\n");
        else {
            emit("contracts identity ramfs labdisk labnet server-relay\r\n");
            emit("hosted FAT32 and kernel/core/net sockets are ELF-only\r\n");
        }
        return 1;
    }
    if (str_eq(verb, "audit")) {
        take_word(cursor, arg, sizeof(arg));
        if (str_eq(arg, "path") || str_eq(arg, "ring")) {
            emit("audit: lab ring\r\n");
            return 1;
        }
        if (s_audit_n == 0) {
            emit("audit empty\r\n");
            return 1;
        }
        for (unsigned i = 0; i < s_audit_n; ++i) {
            emit_uint(i + 1);
            emit(": ");
            emit(s_audit[i]);
            emit("\r\n");
        }
        return 1;
    }
    if (str_eq(verb, "redirect")) {
        if (!take_word(cursor, arg, sizeof(arg))) {
            emit("usage: redirect <file>|off\r\n");
            return 1;
        }
        if (str_eq(arg, "off")) {
            s_redir[0] = 0;
            emit("redirect off\r\n");
            return 1;
        }
        str_copy(s_redir, arg, sizeof(s_redir));
        (void)fl_fs_ramfs_write(session, s_redir, "");
        emit("redirect ");
        emit(s_redir);
        emit("\r\n");
        return 1;
    }
    if (str_eq(verb, "clear")) {
        fl_fs_vga_clear_shell();
        emit("\r\n");
        return 1;
    }
    if (str_eq(verb, "exit")) {
        emit("exit: lab guest stays running\r\n");
        return 1;
    }
    if (str_eq(verb, "bios")) {
        emit("bios: firmware reboot not available in browser lab\r\n");
        return 1;
    }
    if (str_eq(verb, "userdel")) {
        if (!take_word(cursor, arg, sizeof(arg))) {
            emit("usage: userdel <user>\r\n");
            return 1;
        }
        if (!fl_fs_identity_elevated(session))
            emit("need elevation\r\n");
        else if (!fl_fs_identity_userdel(session, arg))
            emit("userdel failed\r\n");
        else {
            emit("deleted ");
            emit(arg);
            emit("\r\n");
        }
        return 1;
    }
    return 0;
}

static void visit_user(const char *name, int elevated, void *ctx)
{
    (void)ctx;
    emit(name);
    emit(elevated ? " elevated\r\n" : "\r\n");
}

void fl_fs_commands_init(void)
{
    s_redir[0] = 0;
    s_audit_n = 0;
    s_loc_n = 0;
    s_lab_netmode = 0;
    s_nl_pod[0] = 0;
    s_nl_memcount = 0;
    s_nl_hostidx = -1;
    s_nl_acting_host_session = -1;
    s_nl_listen_session = -1;
    s_nl_atk_session = -1;
    s_nl_victim_session = -1;
    nl_sb_reset();
}

int fl_fs_commands_run(int session, const char *verb, const char **cursor)
{
    int rc;
    const char *rest = *cursor;
    audit_add(verb, rest);
    if (str_eq(verb, "users")) {
        fl_fs_identity_each_user(visit_user, 0);
        return 1;
    }
    rc = run_meta(session, verb, cursor);
    if (rc == 1)
        return 1;
    if (run_fs(session, verb, cursor) || run_disk(session, verb, cursor) ||
        run_net(verb, cursor) || run_netlab(session, verb, cursor) ||
        run_server(session, verb, cursor))
        return 1;
    return 0;
}
