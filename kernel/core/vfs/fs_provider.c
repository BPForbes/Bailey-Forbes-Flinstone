#include "fs_provider.h"
#include "common.h"
#include "dir_asm.h"
#include "fs_jail.h"
#include "server_shared_fs.h"
#include "fl/path.h"
#include "mem_domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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
static int local_read_bytes(fs_provider_t *p, const char *path, size_t max, uint8_t **data, size_t *len);
static int local_write_bytes(fs_provider_t *p, const char *path, const uint8_t *data, size_t len);
static int local_write_atomic(fs_provider_t *p, const char *path, const uint8_t *data, size_t len);
static int local_dir_open(fs_provider_t *p, const char *dir, fs_dir_ref_t *out);
static void local_dir_close(fs_provider_t *p, fs_dir_ref_t *d);
static int local_dir_read(fs_provider_t *p, const fs_dir_ref_t *d, const char *name, size_t max,
                          uint8_t **data, size_t *len);
static int local_dir_exists(fs_provider_t *p, const fs_dir_ref_t *d, const char *name);
static int local_dir_read_tail(fs_provider_t *p, const fs_dir_ref_t *d, const char *name,
                               size_t unit, size_t keep, uint8_t **data, size_t *len);
static int local_dir_commit(fs_provider_t *p, const fs_dir_ref_t *d, const fs_dir_file_t *files,
                            size_t n);

static const fs_provider_vtable_t local_vtable = {
    local_list, local_read_text, local_write_text,
    local_create_file, local_create_dir, local_delete, local_move,
    local_get_metadata, local_destroy,
    local_read_bytes, local_write_bytes, local_write_atomic,
    local_dir_open, local_dir_close, local_dir_read, local_dir_exists,
    local_dir_read_tail, local_dir_commit
};

static int local_path_ok(const char *path_for_jail) {
    if (!fs_jail_is_active())
        return 0;
    if (fs_jail_check_path(path_for_jail) != 0 || fs_jail_check_home(path_for_jail) != 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static int local_list(fs_provider_t *p, const char *path, fs_node_t **out, int *count) {
    (void)p;
    const char *eff = (path && path[0]) ? path : g_cwd;
    if (local_path_ok(eff) != 0) return -1;
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
        if (path && path[0]) {
            size_t base_len = strnlen(path, FS_PATH_MAX);
            size_t name_len = strnlen(e->d_name, FS_NAME_MAX);
            if (base_len + 1 + name_len >= FS_PATH_MAX) {
                fprintf(stderr, "fs_provider: path overflow: base='%s', entry='%s', limit=%d\n",
                        path, e->d_name, FS_PATH_MAX);
                continue;
            }
            mem_domain_copy(nodes[n].path, path, base_len);
            nodes[n].path[base_len] = '/';
            mem_domain_copy(nodes[n].path + base_len + 1, e->d_name, name_len);
            nodes[n].path[base_len + 1 + name_len] = '\0';
        } else {
            strncpy(nodes[n].path, e->d_name, FS_PATH_MAX - 1);
            nodes[n].path[FS_PATH_MAX - 1] = '\0';
        }
        n++;
    }
    closedir(dp);
    *out = nodes;
    *count = n;
    return 0;
}

static int local_read_text(fs_provider_t *p, const char *path, char *buf, size_t bufsiz) {
    (void)p;
    if (local_path_ok(path) != 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, bufsiz - 1, f);
    buf[n] = '\0';
    fclose(f);
    return (int)n;
}

/* chmod of a regular file by path that never follows a link: open it O_NOFOLLOW, then fchmod. */
static int chmod_nofollow(const char *path, mode_t mode) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    int rc;
    int saved;

    if (fd < 0)
        return -1;
    rc = fchmod(fd, mode);
    saved = errno;
    close(fd);
    errno = saved;
    return rc;
}

/*
 * One writer for local_write_text and local_write_bytes.
 *
 * fl_fs_write_file_atomic already does a full write (stage_at: O_EXCL | FL_FS_O_NOFOLLOW,
 * write until len, fsync) and then renames <path>.part onto path. That rename replaces a
 * final symlink, so the in-place slots do not call it. fl_fs_stage_file is the same full
 * write and leaves the previous file alone. This helper renames that temp onto path only
 * when path is not a symlink, after giving it the permission bits of the regular file it
 * replaces. Any failure removes the temp and leaves the old file as it was.
 * NULL data with len > 0 creates nothing. NULL data with len 0 writes an empty file.
 */
static int local_write_full(const char *path, const uint8_t *data, size_t len) {
    char part[FL_FS_PATH_MAX];
    struct stat st;
    int rc;

    if (!path || (len && !data))
        return -1;
    if (local_path_ok(path) != 0)
        return -1;
    if (snprintf(part, sizeof part, "%s" FL_FS_PART_SUFFIX, path) >= (int)sizeof part) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (fl_fs_stage_file(path, data, len) != 0)
        return -1;
    rc = lstat(path, &st);
    if (rc == 0 && S_ISLNK(st.st_mode)) {
        unlink(part);
        errno = ELOOP;
        return -1;
    }
    if (rc != 0 && errno != ENOENT) {
        int saved = errno;
        unlink(part);
        errno = saved;
        return -1;
    }
    /* Replacing a regular file: the staged file (0600) takes its permission bits. The mode is
     * set through a descriptor opened without following a link, so a link swapped in for the
     * staged file after it was written is refused and never chmod'ed. */
    if (rc == 0 && S_ISREG(st.st_mode) && chmod_nofollow(part, st.st_mode & 0777) != 0) {
        int saved = errno;
        unlink(part);
        errno = saved;
        return -1;
    }
    if (rename(part, path) != 0) {
        int saved = errno;
        unlink(part);
        errno = saved;
        return -1;
    }
    return 0;
}

static int local_write_text(fs_provider_t *p, const char *path, const char *content) {
    (void)p;
    if (!content)
        return -1;
    return local_write_full(path, (const uint8_t *)content, strlen(content));
}

static int local_create_file(fs_provider_t *p, const char *path) {
    (void)p;
    if (local_path_ok(path) != 0) return -1;
    mkdir_parents(path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fclose(f);
    return 0;
}

static int local_create_dir(fs_provider_t *p, const char *path) {
    (void)p;
    if (local_path_ok(path) != 0) return -1;
    mkdir_parents(path);
    return (mkdir(path, 0755) == 0) ? 0 : -1;
}

static int local_delete(fs_provider_t *p, const char *path) {
    (void)p;
    if (local_path_ok(path) != 0) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode))
        return (rmdir(path) == 0) ? 0 : -1;
    return (unlink(path) == 0) ? 0 : -1;
}

static int local_move(fs_provider_t *p, const char *src, const char *dst) {
    (void)p;
    if (local_path_ok(src) != 0) return -1;
    if (local_path_ok(dst) != 0) return -1;
    return (rename(src, dst) == 0) ? 0 : -1;
}

static int local_get_metadata(fs_provider_t *p, const char *path, fs_node_t *out) {
    (void)p;
    if (local_path_ok(path) != 0) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    memset(out, 0, sizeof(*out));
    out->type = S_ISDIR(st.st_mode) ? NODE_DIR : NODE_FILE;
    out->size = (size_t)st.st_size;
    strncpy(out->path, path, FS_PATH_MAX - 1);
    strncpy(out->name, fl_fs_basename(path), FS_NAME_MAX - 1);
    return 0;
}

static void local_destroy(fs_provider_t *p) {
    free(p);
}

static int local_read_bytes(fs_provider_t *p, const char *path, size_t max, uint8_t **data, size_t *len) {
    (void)p;
    if (!path || !data || !len) return -1;
    *data = NULL;
    *len = 0;
    if (local_path_ok(path) != 0) return -1;
    return fl_fs_read_file(path, max, data, len);
}

static int local_write_bytes(fs_provider_t *p, const char *path, const uint8_t *data, size_t len) {
    (void)p;
    return local_write_full(path, data, len);
}

static int local_write_atomic(fs_provider_t *p, const char *path, const uint8_t *data, size_t len) {
    (void)p;
    if (!path || (!data && len)) return -1;
    if (local_path_ok(path) != 0) return -1;
    return fl_fs_write_file_atomic(path, data, len);
}

/*
 * Contained calls: thin over the placement layer (server_shared_fs.c), which decides how a
 * name lands. The jail is checked once, on the directory, when it is held; the names inside
 * are plain and cannot leave it. Where the contained calls exist a directory that cannot be
 * held is refused (the same rule as fl_fs_exists_at: never a silent path fallback).
 */
static int local_dir_open(fs_provider_t *p, const char *dir, fs_dir_ref_t *out) {
    (void)p;
    if (!dir || !out) return -1;
    out->fd = -1;
    out->dir = dir;
    if (local_path_ok(dir) != 0) return -1;
    if (!fl_fs_contained_available()) return 0;
    out->fd = fl_fs_dir_open(dir);
    return out->fd >= 0 ? 0 : -1;
}

static void local_dir_close(fs_provider_t *p, fs_dir_ref_t *d) {
    (void)p;
    if (!d) return;
    if (d->fd >= 0) close(d->fd);
    d->fd = -1;
}

static int local_dir_read(fs_provider_t *p, const fs_dir_ref_t *d, const char *name, size_t max,
                          uint8_t **data, size_t *len) {
    (void)p;
    if (!d) return -1;
    return fl_fs_read_file_at(d->fd, d->dir, name, max, data, len);
}

static int local_dir_exists(fs_provider_t *p, const fs_dir_ref_t *d, const char *name) {
    (void)p;
    if (!d) return -1;
    return fl_fs_exists_at(d->fd, d->dir, name);
}

static int local_dir_read_tail(fs_provider_t *p, const fs_dir_ref_t *d, const char *name,
                               size_t unit, size_t keep, uint8_t **data, size_t *len) {
    (void)p;
    if (!d) return -1;
    return fl_fs_read_tail_at(d->fd, d->dir, name, unit, keep, data, len);
}

static int local_dir_commit(fs_provider_t *p, const fs_dir_ref_t *d, const fs_dir_file_t *files,
                            size_t n) {
    fl_fs_file_out_t out[FL_FS_COMMIT_FILES_MAX];
    size_t i;

    (void)p;
    if (!d || !files || n == 0 || n > FL_FS_COMMIT_FILES_MAX) return -1;
    for (i = 0; i < n; i++) {
        out[i].name = files[i].name;
        out[i].data = files[i].data;
        out[i].len = files[i].len;
    }
    return fl_fs_commit_files(d->fd, d->dir, out, n);
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

/* The stub has no storage: the byte slots say so explicitly instead of staying NULL. */
static int memory_read_bytes(fs_provider_t *p, const char *path, size_t max, uint8_t **data, size_t *len) {
    (void)p; (void)path; (void)max;
    if (data) *data = NULL;
    if (len) *len = 0;
    return -1;
}

static int memory_write_bytes(fs_provider_t *p, const char *path, const uint8_t *data, size_t len) {
    (void)p; (void)path; (void)data; (void)len;
    return -1;
}

static const fs_provider_vtable_t memory_vtable = {
    memory_list, memory_read_text, memory_write_text,
    memory_create_file, memory_create_dir, memory_delete, memory_move,
    memory_get_metadata, memory_destroy,
    memory_read_bytes, memory_write_bytes, memory_write_bytes,
    /* no contained slots: the memory provider has no directory to hold */
    NULL, NULL, NULL, NULL, NULL, NULL
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
int fs_provider_read_bytes(fs_provider_t *p, const char *path, size_t max, uint8_t **data, size_t *len) {
    if (data) *data = NULL;
    if (len) *len = 0;
    if (!p || !p->vtable || !p->vtable->read_bytes) { errno = ENOSYS; return -1; }
    return p->vtable->read_bytes(p, path, max, data, len);
}
int fs_provider_write_bytes(fs_provider_t *p, const char *path, const uint8_t *data, size_t len) {
    if (!p || !p->vtable || !p->vtable->write_bytes) { errno = ENOSYS; return -1; }
    return p->vtable->write_bytes(p, path, data, len);
}
int fs_provider_write_atomic(fs_provider_t *p, const char *path, const uint8_t *data, size_t len) {
    if (!p || !p->vtable || !p->vtable->write_atomic) { errno = ENOSYS; return -1; }
    return p->vtable->write_atomic(p, path, data, len);
}
int fs_provider_dir_open(fs_provider_t *p, const char *dir, fs_dir_ref_t *out) {
    if (out) { out->fd = -1; out->dir = NULL; }
    if (!p || !p->vtable || !p->vtable->dir_open) { errno = ENOSYS; return -1; }
    return p->vtable->dir_open(p, dir, out);
}
void fs_provider_dir_close(fs_provider_t *p, fs_dir_ref_t *d) {
    if (p && p->vtable && p->vtable->dir_close) p->vtable->dir_close(p, d);
}
int fs_provider_dir_read(fs_provider_t *p, const fs_dir_ref_t *d, const char *name, size_t max,
                         uint8_t **data, size_t *len) {
    if (data) *data = NULL;
    if (len) *len = 0;
    if (!p || !p->vtable || !p->vtable->dir_read) { errno = ENOSYS; return -1; }
    return p->vtable->dir_read(p, d, name, max, data, len);
}
int fs_provider_dir_exists(fs_provider_t *p, const fs_dir_ref_t *d, const char *name) {
    if (!p || !p->vtable || !p->vtable->dir_exists) { errno = ENOSYS; return -1; }
    return p->vtable->dir_exists(p, d, name);
}
int fs_provider_dir_read_tail(fs_provider_t *p, const fs_dir_ref_t *d, const char *name,
                              size_t unit, size_t keep, uint8_t **data, size_t *len) {
    if (data) *data = NULL;
    if (len) *len = 0;
    if (!p || !p->vtable || !p->vtable->dir_read_tail) { errno = ENOSYS; return -1; }
    return p->vtable->dir_read_tail(p, d, name, unit, keep, data, len);
}
int fs_provider_dir_commit(fs_provider_t *p, const fs_dir_ref_t *d, const fs_dir_file_t *files,
                           size_t n) {
    if (!p || !p->vtable || !p->vtable->dir_commit) { errno = ENOSYS; return -1; }
    return p->vtable->dir_commit(p, d, files, n);
}
void fs_provider_destroy(fs_provider_t *p) {
    p->vtable->destroy(p);
}

void fs_nodes_free(fs_node_t *nodes, int count) {
    (void)count;
    free(nodes);
}
