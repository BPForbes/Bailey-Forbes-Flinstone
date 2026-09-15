#include "ramfs.h"
#include "identity.h"

#define NODES 16
#define NAME 20

enum {
    FS_OK = 0,
    FS_NOTFOUND = -1,
    FS_NOTDIR = -2,
    FS_EXISTS = -3,
    FS_FULL = -4,
    FS_NOTFILE = -5,
    FS_BAD = -6,
    FS_NOTEMPTY = -7,
};

struct node {
    char name[NAME];
    int used;
    int is_dir;
    int parent;
    unsigned size;
    char data[FL_FS_RAMFS_DATA];
};

static struct node s_nodes[NODES];
static int s_cwd[FL_FS_MAX_SESSIONS];

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

static int valid_name(const char *name)
{
    unsigned n = 0;
    if (!name || !name[0] || str_eq(name, ".") || str_eq(name, ".."))
        return 0;
    while (name[n]) {
        char c = name[n++];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok || n >= NAME)
            return 0;
    }
    return 1;
}

static int find_child(int parent, const char *name)
{
    for (int i = 0; i < NODES; ++i) {
        if (s_nodes[i].used && s_nodes[i].parent == parent && str_eq(s_nodes[i].name, name))
            return i;
    }
    return -1;
}

static int alloc_node(int parent, const char *name, int is_dir)
{
    int slot = -1;
    if (!valid_name(name) || parent < 0 || !s_nodes[parent].used || !s_nodes[parent].is_dir)
        return FS_BAD;
    if (find_child(parent, name) >= 0)
        return FS_EXISTS;
    for (int i = 0; i < NODES; ++i) {
        if (!s_nodes[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return FS_FULL;
    s_nodes[slot].used = 1;
    s_nodes[slot].is_dir = is_dir;
    s_nodes[slot].parent = parent;
    s_nodes[slot].size = 0;
    s_nodes[slot].data[0] = 0;
    str_copy(s_nodes[slot].name, name, NAME);
    return slot;
}

static int walk(int start, const char *path, int create_file, const char **text)
{
    int node = start;
    unsigned i = 0;
    if (!path || !path[0])
        return node;
    if (path[0] == '/') {
        node = 0;
        ++i;
    }
    while (path[i]) {
        char part[NAME];
        unsigned n = 0;
        int last;
        while (path[i] == '/')
            ++i;
        if (!path[i])
            break;
        while (path[i] && path[i] != '/' && n + 1 < NAME)
            part[n++] = path[i++];
        part[n] = 0;
        if (path[i] && path[i] != '/')
            return FS_BAD;
        last = !path[i] || path[i + 1] == 0;
        while (path[i] == '/')
            ++i;
        last = last || !path[i];
        if (str_eq(part, "."))
            continue;
        if (str_eq(part, "..")) {
            if (s_nodes[node].parent >= 0)
                node = s_nodes[node].parent;
            continue;
        }
        {
            int child = find_child(node, part);
            if (child >= 0) {
                node = child;
                continue;
            }
            if (create_file && last && text) {
                int made = alloc_node(node, part, 0);
                return made;
            }
            return FS_NOTFOUND;
        }
    }
    (void)text;
    return node;
}

static int session_ok(int session)
{
    return session >= 0 && session < FL_FS_MAX_SESSIONS;
}

void fl_fs_ramfs_init(void)
{
    int notes;
    for (int i = 0; i < NODES; ++i) {
        s_nodes[i].used = 0;
        s_nodes[i].parent = -1;
    }
    s_nodes[0].used = 1;
    s_nodes[0].is_dir = 1;
    s_nodes[0].parent = -1;
    s_nodes[0].name[0] = 0;
    for (int i = 0; i < FL_FS_MAX_SESSIONS; ++i)
        s_cwd[i] = 0;
    (void)fl_fs_ramfs_write(0, "/readme.txt", "Flintstone lab ramfs. Not the hosted FAT32 volume.");
    notes = alloc_node(0, "notes", 1);
    if (notes >= 0)
        (void)fl_fs_ramfs_write(0, "/notes/welcome.txt", "dir cat write mkdir rm pwd cd");
}

void fl_fs_ramfs_pwd(int session, char *out, unsigned cap)
{
    int stack[NODES];
    int depth = 0;
    int node = session_ok(session) ? s_cwd[session] : 0;
    unsigned used = 0;
    if (!out || cap == 0)
        return;
    if (node <= 0) {
        str_copy(out, "/", cap);
        return;
    }
    while (node > 0 && depth < NODES) {
        stack[depth++] = node;
        node = s_nodes[node].parent;
    }
    out[0] = 0;
    while (depth--) {
        if (used + 1 < cap)
            out[used++] = '/';
        str_copy(out + used, s_nodes[stack[depth]].name, cap > used ? cap - used : 0);
        used = str_len(out);
    }
    if (!out[0])
        str_copy(out, "/", cap);
}

int fl_fs_ramfs_cd(int session, const char *path)
{
    int node;
    if (!session_ok(session))
        return FS_BAD;
    node = walk(s_cwd[session], path, 0, 0);
    if (node < 0)
        return node;
    if (!s_nodes[node].is_dir)
        return FS_NOTDIR;
    s_cwd[session] = node;
    return FS_OK;
}

int fl_fs_ramfs_mkdir(int session, const char *path)
{
    char part[NAME];
    const char *slash;
    int parent;
    unsigned n = 0;
    if (!session_ok(session) || !path || !path[0])
        return FS_BAD;
    slash = path;
    while (*slash)
        ++slash;
    while (slash > path && slash[-1] == '/')
        --slash;
    while (slash > path && slash[-1] != '/')
        --slash;
    if (slash == path && path[0] != '/')
        parent = s_cwd[session];
    else {
        char prefix[FL_FS_RAMFS_PATH];
        unsigned plen = 0;
        const char *p = path;
        while (p < slash && plen + 1 < sizeof(prefix))
            prefix[plen++] = *p++;
        prefix[plen] = 0;
        parent = walk(s_cwd[session], prefix[0] ? prefix : "/", 0, 0);
    }
    if (parent < 0)
        return parent;
    while (*slash && *slash != '/' && n + 1 < NAME)
        part[n++] = *slash++;
    part[n] = 0;
    {
        int made = alloc_node(parent, part, 1);
        return made < 0 ? made : FS_OK;
    }
}

int fl_fs_ramfs_write(int session, const char *path, const char *text)
{
    int node;
    if (!session_ok(session) || !path || !path[0] || !text)
        return FS_BAD;
    node = walk(s_cwd[session], path, 1, &text);
    if (node < 0)
        return node;
    if (s_nodes[node].is_dir)
        return FS_NOTFILE;
    str_copy(s_nodes[node].data, text, FL_FS_RAMFS_DATA);
    s_nodes[node].size = str_len(s_nodes[node].data);
    return FS_OK;
}

int fl_fs_ramfs_cat(int session, const char *path, char *out, unsigned cap)
{
    int node;
    if (!session_ok(session) || !path || !out || cap == 0)
        return FS_BAD;
    node = walk(s_cwd[session], path, 0, 0);
    if (node < 0)
        return node;
    if (s_nodes[node].is_dir)
        return FS_NOTFILE;
    str_copy(out, s_nodes[node].data, cap);
    return FS_OK;
}

int fl_fs_ramfs_rm(int session, const char *path)
{
    int node;
    if (!session_ok(session) || !path || !path[0] || str_eq(path, "/"))
        return FS_BAD;
    node = walk(s_cwd[session], path, 0, 0);
    if (node < 0)
        return node;
    if (node == 0)
        return FS_BAD;
    if (s_nodes[node].is_dir) {
        for (int i = 0; i < NODES; ++i) {
            if (s_nodes[i].used && s_nodes[i].parent == node)
                return FS_NOTEMPTY;
        }
    }
    for (int i = 0; i < FL_FS_MAX_SESSIONS; ++i) {
        if (s_cwd[i] == node)
            s_cwd[i] = s_nodes[node].parent >= 0 ? s_nodes[node].parent : 0;
    }
    s_nodes[node].used = 0;
    return FS_OK;
}

int fl_fs_ramfs_dir(int session, const char *path,
                    void (*visit)(const char *name, int is_dir, unsigned size, void *ctx),
                    void *ctx)
{
    int node;
    int start = session_ok(session) ? s_cwd[session] : 0;
    if (!visit)
        return FS_BAD;
    node = walk(start, path && path[0] ? path : ".", 0, 0);
    if (node < 0)
        return node;
    if (!s_nodes[node].is_dir)
        return FS_NOTDIR;
    for (int i = 0; i < NODES; ++i) {
        if (s_nodes[i].used && s_nodes[i].parent == node)
            visit(s_nodes[i].name, s_nodes[i].is_dir, s_nodes[i].size, ctx);
    }
    return FS_OK;
}
