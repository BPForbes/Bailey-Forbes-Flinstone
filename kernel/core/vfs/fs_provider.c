#include "fs_provider.h"
#include "common.h"
#include "dir_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

/* Create parent directories for path (mkdir -p style) */
static int mkdir_parents(const char *path) {
    char tmp[FS_PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *p = strrchr(tmp, '/');
    if (!p || p == tmp) return 0;
    *p = '\0';
    struct stat st;
    if (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    if (mkdir_parents(tmp) != 0) return -1;
    return (mkdir(tmp, 0755) == 0) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * LocalFileSystemProvider - uses host filesystem (opendir, fopen, etc.)
 * --------------------------------------------------------------------------- */
static int local_list(fs_provider_t *p, const char *path, fs_node_t **out, int *count);
static int local_read_text(fs_provider_t *p, const char *path, char *buf, size_t bufsiz);
static int local_write_text(fs_provider_t *p, const char *path, const char *content);
static int local_create_file(fs_provider_t *p, const char *path);
static int local_create_dir(fs_provider_t *p, const char *path);
static int local_delete(fs_provider_t *p, const char *path);
static int local_move(fs_provider_t *p, const char *src, const char *dst);
static int local_get_metadata(fs_provider_t *p, const char *path, fs_node_t *out);
static void local_destroy(fs_provider_t *p);

static const fs_provider_vtable_t local_vtable = {
    local_list, local_read_text, local_write_text,
    local_create_file, local_create_dir, local_delete, local_move,
    local_get_metadata, local_destroy
};

static int local_list(fs_provider_t *p, const char *path, fs_node_t **out, int *count) {
    (void)p;
    DIR *dp = opendir(path ? path : ".");
    if (!dp) return -1;
    fs_node_t *nodes = malloc(sizeof(fs_node_t) * 256);
    if (!nodes) { closedir(dp); return -1; }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(dp)) && n < 256) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        dir_asm_zero(&nodes[n], sizeof(nodes[n]));
        strncpy(nodes[n].name, e->d_name, FS_NAME_MAX - 1);
        nodes[n].type = (e->d_type == DT_DIR) ? NODE_DIR : NODE_FILE;
        if (path && path[0])
            snprintf(nodes[n].path, FS_PATH_MAX, "%s/%s", path, e->d_name);
        else
            snprintf(nodes[n].path, FS_PATH_MAX, "%s", e->d_name);
        n++;
    }
    closedir(dp);
    *out = nodes;
    *count = n;
    return 0;
}

static int local_read_text(fs_provider_t *p, const char *path, char *buf, size_t bufsiz) {
    (void)p;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, bufsiz - 1, f);
    buf[n] = '\0';
    fclose(f);
    return (int)n;
}

static int local_write_text(fs_provider_t *p, const char *path, const char *content) {
    (void)p;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    size_t len = strlen(content);
    size_t w = fwrite(content, 1, len, f);
    fclose(f);
    return (w == len) ? 0 : -1;
}

static int local_create_file(fs_provider_t *p, const char *path) {
    (void)p;
    mkdir_parents(path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fclose(f);
    return 0;
}

static int local_create_dir(fs_provider_t *p, const char *path) {
    (void)p;
    mkdir_parents(path);
    return (mkdir(path, 0755) == 0) ? 0 : -1;
}

static int local_delete(fs_provider_t *p, const char *path) {
    (void)p;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode))
        return (rmdir(path) == 0) ? 0 : -1;
    return (unlink(path) == 0) ? 0 : -1;
}

static int local_move(fs_provider_t *p, const char *src, const char *dst) {
    (void)p;
    return (rename(src, dst) == 0) ? 0 : -1;
}

static int local_get_metadata(fs_provider_t *p, const char *path, fs_node_t *out) {
    (void)p;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    memset(out, 0, sizeof(*out));
    out->type = S_ISDIR(st.st_mode) ? NODE_DIR : NODE_FILE;
    out->size = (size_t)st.st_size;
    strncpy(out->path, path, FS_PATH_MAX - 1);
    const char *base = strrchr(path, '/');
    strncpy(out->name, base ? base + 1 : path, FS_NAME_MAX - 1);
    return 0;
}

static void local_destroy(fs_provider_t *p) {
    free(p);
}

fs_provider_t *fs_local_provider_create(void) {
    fs_provider_t *p = calloc(1, sizeof(*p));
    if (p) p->vtable = &local_vtable;
    return p;
}

/* ---------------------------------------------------------------------------
 * InMemoryFileSystemProvider - stub for testing (uses hash/tree)
 * For now delegates to local with a temp dir or returns empty
 * --------------------------------------------------------------------------- */
static int memory_list(fs_provider_t *p, const char *path, fs_node_t **out, int *count) {
    (void)p; (void)path;
    *out = malloc(sizeof(fs_node_t));
    if (!*out) return -1;
    memset(*out, 0, sizeof(**out));
    *count = 0;
    return 0;
}

static int memory_read_text(fs_provider_t *p, const char *path, char *buf, size_t bufsiz) {
    (void)p; (void)path; (void)buf; (void)bufsiz;
    return -1;
}

static int memory_write_text(fs_provider_t *p, const char *path, const char *content) {
    (void)p; (void)path; (void)content;
    return -1;
}

static int memory_create_file(fs_provider_t *p, const char *path) {
    (void)p; (void)path;
    return -1;
}

static int memory_create_dir(fs_provider_t *p, const char *path) {
    (void)p; (void)path;
    return -1;
}

static int memory_delete(fs_provider_t *p, const char *path) {
    (void)p; (void)path;
    return -1;
}

static int memory_move(fs_provider_t *p, const char *src, const char *dst) {
    (void)p; (void)src; (void)dst;
    return -1;
}

static int memory_get_metadata(fs_provider_t *p, const char *path, fs_node_t *out) {
    (void)p; (void)path; (void)out;
    return -1;
}

static void memory_destroy(fs_provider_t *p) {
    free(p);
}

static const fs_provider_vtable_t memory_vtable = {
    memory_list, memory_read_text, memory_write_text,
    memory_create_file, memory_create_dir, memory_delete, memory_move,
    memory_get_metadata, memory_destroy
};

fs_provider_t *fs_memory_provider_create(void) {
    fs_provider_t *p = calloc(1, sizeof(*p));
    if (p) p->vtable = &memory_vtable;
    return p;
}

/* ---------------------------------------------------------------------------
 * Convenience wrappers
 * --------------------------------------------------------------------------- */
int fs_provider_list(fs_provider_t *p, const char *path, fs_node_t **out, int *count) {
    return p->vtable->list_entries(p, path, out, count);
}
int fs_provider_read_text(fs_provider_t *p, const char *path, char *buf, size_t bufsiz) {
    return p->vtable->read_text(p, path, buf, bufsiz);
}
int fs_provider_write_text(fs_provider_t *p, const char *path, const char *content) {
    return p->vtable->write_text(p, path, content);
}
int fs_provider_create_file(fs_provider_t *p, const char *path) {
    return p->vtable->create_file(p, path);
}
int fs_provider_create_dir(fs_provider_t *p, const char *path) {
    return p->vtable->create_dir(p, path);
}
int fs_provider_delete(fs_provider_t *p, const char *path) {
    return p->vtable->delete_path(p, path);
}
int fs_provider_move(fs_provider_t *p, const char *src, const char *dst) {
    return p->vtable->move_path(p, src, dst);
}
void fs_provider_destroy(fs_provider_t *p) {
    p->vtable->destroy(p);
}

void fs_nodes_free(fs_node_t *nodes, int count) {
    (void)count;
    free(nodes);
}
