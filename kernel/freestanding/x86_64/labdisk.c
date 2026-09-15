#include "labdisk.h"

#define CL_MAX 8
#define CL_SZ 32
#define DF_MAX 8
#define DF_NAME 16
#define DF_DATA 48

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

static int contains(const char *hay, const char *needle)
{
    unsigned i, j;
    if (!needle || !needle[0])
        return 1;
    for (i = 0; hay[i]; ++i) {
        for (j = 0; needle[j] && hay[i + j] && hay[i + j] == needle[j]; ++j)
            ;
        if (!needle[j])
            return 1;
    }
    return 0;
}

static const char *leaf(const char *path)
{
    const char *p = path;
    const char *last = path;
    if (!path || !path[0])
        return "";
    while (*p) {
        if (*p == '/' || *p == '\\')
            last = p + 1;
        ++p;
    }
    return last[0] ? last : path;
}

struct cluster {
    int used;
    char data[CL_SZ];
};

struct dfile {
    int used;
    int is_dir;
    char name[DF_NAME];
    char data[DF_DATA];
    unsigned size;
};

static struct {
    int ready;
    char volume[DF_NAME];
    char backing[DF_NAME];
    unsigned rows;
    unsigned nibbles;
    unsigned ncl;
    struct cluster cl[CL_MAX];
    struct dfile files[DF_MAX];
} d;

static void reset_files(void)
{
    for (int i = 0; i < DF_MAX; ++i)
        d.files[i].used = 0;
}

static int find_file(const char *name)
{
    const char *n = leaf(name);
    for (int i = 0; i < DF_MAX; ++i) {
        if (d.files[i].used && str_eq(d.files[i].name, n))
            return i;
    }
    return -1;
}

static int alloc_file(const char *name, int is_dir)
{
    const char *n = leaf(name);
    if (!n[0] || find_file(n) >= 0)
        return -1;
    for (int i = 0; i < DF_MAX; ++i) {
        if (d.files[i].used)
            continue;
        d.files[i].used = 1;
        d.files[i].is_dir = is_dir;
        d.files[i].size = 0;
        d.files[i].data[0] = 0;
        str_copy(d.files[i].name, n, DF_NAME);
        return i;
    }
    return -2;
}

static void ready_default(void)
{
    if (d.ready)
        return;
    (void)fl_fs_labdisk_create("lab", 4, 8);
}

void fl_fs_labdisk_init(void)
{
    d.ready = 0;
    d.ncl = 0;
    d.volume[0] = 0;
    d.backing[0] = 0;
    reset_files();
    for (int i = 0; i < CL_MAX; ++i)
        d.cl[i].used = 0;
    (void)fl_fs_labdisk_create("lab", 4, 8);
}

int fl_fs_labdisk_create(const char *volume, unsigned rows, unsigned nibbles)
{
    unsigned n;
    if (!volume || !volume[0])
        return -1;
    n = rows ? rows : 4;
    if (n > CL_MAX)
        n = CL_MAX;
    d.ready = 1;
    d.ncl = n;
    d.rows = rows ? rows : n;
    d.nibbles = nibbles ? nibbles : 8;
    str_copy(d.volume, volume, sizeof(d.volume));
    str_copy(d.backing, volume, sizeof(d.backing));
    for (int i = 0; i < CL_MAX; ++i) {
        d.cl[i].used = 0;
        d.cl[i].data[0] = 0;
    }
    reset_files();
    return 0;
}

int fl_fs_labdisk_format(const char *backing, const char *volume, unsigned rows, unsigned nibbles)
{
    int rc = fl_fs_labdisk_create(volume && volume[0] ? volume : "lab", rows, nibbles);
    if (rc == 0 && backing && backing[0])
        str_copy(d.backing, backing, sizeof(d.backing));
    return rc;
}

int fl_fs_labdisk_set(const char *backing)
{
    ready_default();
    if (!backing || !backing[0])
        return -1;
    str_copy(d.backing, backing, sizeof(d.backing));
    return 0;
}

int fl_fs_labdisk_initgeom(unsigned count, unsigned size)
{
    (void)size;
    return fl_fs_labdisk_create(d.volume[0] ? d.volume : "lab", count, d.nibbles ? d.nibbles : 8);
}

void fl_fs_labdisk_print(void (*emit)(const char *), void (*emit_uint)(unsigned))
{
    ready_default();
    emit("disk ");
    emit(d.backing);
    emit(" volume ");
    emit(d.volume);
    emit(" clusters ");
    emit_uint(d.ncl);
    emit(" rows ");
    emit_uint(d.rows);
    emit(" nibbles ");
    emit_uint(d.nibbles);
    emit("\r\n");
    fl_fs_labdisk_list(emit, emit_uint);
}

void fl_fs_labdisk_list(void (*emit)(const char *), void (*emit_uint)(unsigned))
{
    int any = 0;
    ready_default();
    for (unsigned i = 0; i < d.ncl; ++i) {
        emit("#");
        emit_uint(i);
        emit(" ");
        if (d.cl[i].used)
            emit(d.cl[i].data[0] ? d.cl[i].data : "(set)");
        else
            emit("-");
        emit("\r\n");
        any = 1;
    }
    if (!any)
        emit("no clusters\r\n");
}

int fl_fs_labdisk_write_cluster(unsigned idx, const char *text)
{
    ready_default();
    if (idx >= d.ncl || !text)
        return -1;
    d.cl[idx].used = 1;
    str_copy(d.cl[idx].data, text, CL_SZ);
    return 0;
}

int fl_fs_labdisk_del_cluster(unsigned idx)
{
    ready_default();
    if (idx >= d.ncl)
        return -1;
    d.cl[idx].used = 0;
    d.cl[idx].data[0] = 0;
    return 0;
}

int fl_fs_labdisk_add_cluster(const char *text)
{
    ready_default();
    if (d.ncl >= CL_MAX)
        return -1;
    {
        unsigned idx = d.ncl++;
        d.cl[idx].used = 1;
        str_copy(d.cl[idx].data, text ? text : "", CL_SZ);
    }
    return 0;
}

int fl_fs_labdisk_put(const char *name, const char *data)
{
    int slot;
    ready_default();
    if (!name || !name[0] || !data)
        return -1;
    slot = find_file(name);
    if (slot < 0)
        slot = alloc_file(name, 0);
    if (slot < 0)
        return slot;
    if (d.files[slot].is_dir)
        return -3;
    str_copy(d.files[slot].data, data, DF_DATA);
    d.files[slot].size = str_len(d.files[slot].data);
    return 0;
}

int fl_fs_labdisk_get(const char *name, char *out, unsigned cap)
{
    int slot;
    ready_default();
    if (!name || !out || cap == 0)
        return -1;
    slot = find_file(name);
    if (slot < 0 || d.files[slot].is_dir)
        return -1;
    str_copy(out, d.files[slot].data, cap);
    return 0;
}

int fl_fs_labdisk_del_file(const char *name)
{
    int slot;
    ready_default();
    slot = find_file(name);
    if (slot < 0)
        return -1;
    if (d.files[slot].is_dir)
        return -3;
    d.files[slot].used = 0;
    return 0;
}

int fl_fs_labdisk_mkdir(const char *name)
{
    ready_default();
    if (!name || !name[0])
        return -1;
    if (find_file(name) >= 0)
        return -4;
    return alloc_file(name, 1) >= 0 ? 0 : -2;
}

void fl_fs_labdisk_files(void (*visit)(const char *name, int is_dir, unsigned size, void *ctx), void *ctx)
{
    ready_default();
    if (!visit)
        return;
    for (int i = 0; i < DF_MAX; ++i) {
        if (d.files[i].used)
            visit(d.files[i].name, d.files[i].is_dir, d.files[i].size, ctx);
    }
}

int fl_fs_labdisk_search(const char *text,
                         void (*visit)(const char *path, void *ctx), void *ctx)
{
    ready_default();
    if (!text || !visit)
        return -1;
    for (unsigned i = 0; i < d.ncl; ++i) {
        if (d.cl[i].used && contains(d.cl[i].data, text))
            visit("cluster", ctx);
    }
    for (int i = 0; i < DF_MAX; ++i) {
        if (d.files[i].used && (contains(d.files[i].name, text) || contains(d.files[i].data, text)))
            visit(d.files[i].name, ctx);
    }
    return 0;
}

void fl_fs_labdisk_usage(unsigned *bytes, unsigned *clusters)
{
    unsigned b = 0, n = 0;
    ready_default();
    for (unsigned i = 0; i < d.ncl; ++i) {
        if (d.cl[i].used)
            n++;
    }
    for (int i = 0; i < DF_MAX; ++i) {
        if (d.files[i].used && !d.files[i].is_dir)
            b += d.files[i].size;
    }
    if (bytes)
        *bytes = b;
    if (clusters)
        *clusters = n;
}
