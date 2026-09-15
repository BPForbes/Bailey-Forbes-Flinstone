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
    char v4[16];
    char v6[16];
    if (fl_fs_labnet_resolve(host, v4, sizeof(v4), v6, sizeof(v6)) != 0) {
        emit("nxdomain ");
        emit(host);
        emit("\r\n");
        return;
    }
    emit("Name: ");
    emit(host);
    emit("\r\nA ");
    emit(v4);
    emit("\r\nAAAA ");
    emit(v6);
    emit("\r\n");
}

static int run_net(const char *verb, const char **cursor)
{
    char a[32];
    char b[32];
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

static int run_server(const char *verb, const char **cursor)
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

static int run_meta(int session, const char *verb, const char **cursor)
{
    char arg[24];
    if (str_eq(verb, "help")) {
        emit("whoami users login logout su sudo useradd userdel passwd switchuser session\r\n");
        emit("dir ls pwd cd cat type write make mkdir rmdir rmtree rm mv du search listdirs loc where\r\n");
        emit("createdisk format initdisk setdisk printdisk listclusters writecluster delcluster addcluster update import\r\n");
        emit("diskput diskget diskfiles diskmkdir diskdel\r\n");
        emit("ping ping6 check ifconfig arp route netstat nslookup resolve netsh wifi udpsend udplisten\r\n");
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
        run_net(verb, cursor) || run_server(verb, cursor))
        return 1;
    return 0;
}
