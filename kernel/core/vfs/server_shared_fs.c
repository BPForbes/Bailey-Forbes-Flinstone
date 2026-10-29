#include "server_shared_fs.h"

#include "common.h"
#include "contract_p5_server_share.h"
#include "server_shared_db.h"
#include "fl/path.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FL_SERVER_SHARED_META_SUFFIX ".meta"

int fl_fs_mkdir_p(const char *path, mode_t mode)
{
    char tmp[FL_FS_PATH_MAX];
    size_t len;

    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    len = strlen(path);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(tmp, path, len + 1u);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/')
            continue;
        tmp[i] = '\0';
        if (mkdir(tmp, mode) != 0 && errno != EEXIST)
            return -1;
        tmp[i] = '/';
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/*
 * Contained access. Where the platform has the *at calls, a directory is held as an fd and a
 * name is opened relative to it, refusing links (O_NOFOLLOW). A path-based call is the same
 * code with AT_FDCWD as the directory.
 */
#if defined(AT_FDCWD) && defined(O_NOFOLLOW) && defined(O_DIRECTORY) && \
    defined(AT_SYMLINK_NOFOLLOW)
#define FL_FS_HAVE_OPENAT 1
#define FL_FS_CWDFD AT_FDCWD
#else
#define FL_FS_CWDFD (-1)
#endif

#ifdef O_NOFOLLOW
#define FL_FS_O_NOFOLLOW O_NOFOLLOW
#else
#define FL_FS_O_NOFOLLOW 0
#endif

/**
 * Longest single name the contained calls take, with room for ".part" (5 bytes) under
 * FL_FS_NAME_MAX. ".kqbak" is 6 and a copied backup stages ".kqbak.part" (11), so
 * fl_fs_commit_files checks its own, tighter, limit.
 */
#define FL_FS_LEAF_MAX 250u

static int at_open(int dirfd, const char *path, int flags, mode_t mode)
{
#ifdef FL_FS_HAVE_OPENAT
    return openat(dirfd, path, flags, mode);
#else
    (void)dirfd;
    return open(path, flags, mode);
#endif
}

static int at_lstat(int dirfd, const char *path, struct stat *st)
{
#ifdef FL_FS_HAVE_OPENAT
    return fstatat(dirfd, path, st, AT_SYMLINK_NOFOLLOW);
#else
    (void)dirfd;
    return lstat(path, st);
#endif
}

static int at_unlink(int dirfd, const char *path)
{
#ifdef FL_FS_HAVE_OPENAT
    return unlinkat(dirfd, path, 0);
#else
    (void)dirfd;
    return unlink(path);
#endif
}

static int at_rename(int dirfd, const char *from, const char *to)
{
#ifdef FL_FS_HAVE_OPENAT
    return renameat(dirfd, from, dirfd, to);
#else
    (void)dirfd;
    return rename(from, to);
#endif
}

/** A single name inside a directory: not empty, no separator, not "." or "..", fits with ".part". */
static int leaf_name_ok(const char *name)
{
    if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, "..") ||
        strchr(name, '/') || strchr(name, '\\')) {
        errno = EINVAL;
        return 0;
    }
    if (strlen(name) > FL_FS_LEAF_MAX) {
        errno = ENAMETOOLONG;
        return 0;
    }
    return 1;
}

/*
 * Writes data to "<path>.part" relative to dirfd. The file is created new (O_EXCL, never
 * through a link). A leftover at that name is removed and the create tried once more; a
 * link there is removed itself, never written through.
 */
static int stage_at(int dirfd, const char *path, const uint8_t *data, size_t len)
{
    char tmp[FL_FS_PATH_MAX];
    struct stat st;
    int fd;
    size_t off = 0;
    const int flags = O_WRONLY | O_CREAT | O_EXCL | FL_FS_O_NOFOLLOW | O_CLOEXEC;

    if (!path || (len && !data) ||
        snprintf(tmp, sizeof tmp, "%s" FL_FS_PART_SUFFIX, path) >= (int)sizeof tmp)
        return -1;
    fd = at_open(dirfd, tmp, flags, 0600);
    if (fd < 0 && errno == EEXIST && at_lstat(dirfd, tmp, &st) == 0 && !S_ISDIR(st.st_mode) &&
        at_unlink(dirfd, tmp) == 0)
        fd = at_open(dirfd, tmp, flags, 0600);
    if (fd < 0)
        return -1;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0) {
            close(fd);
            at_unlink(dirfd, tmp);
            return -1;
        }
        off += (size_t)w;
    }
    if (fsync(fd) != 0) {
        close(fd);
        at_unlink(dirfd, tmp);
        return -1;
    }
    if (close(fd) != 0) {
        at_unlink(dirfd, tmp);
        return -1;
    }
    return 0;
}

static int write_atomic_at(int dirfd, const char *path, const uint8_t *data, size_t len)
{
    char tmp[FL_FS_PATH_MAX];

    if (!path || snprintf(tmp, sizeof tmp, "%s" FL_FS_PART_SUFFIX, path) >= (int)sizeof tmp ||
        stage_at(dirfd, path, data, len) != 0)
        return -1;
    if (at_rename(dirfd, tmp, path) != 0) {
        at_unlink(dirfd, tmp);
        return -1;
    }
    return 0;
}

int fl_fs_stage_file(const char *path, const uint8_t *data, size_t len)
{
    return stage_at(FL_FS_CWDFD, path, data, len);
}

int fl_fs_write_file_atomic(const char *path, const uint8_t *data, size_t len)
{
    return write_atomic_at(FL_FS_CWDFD, path, data, len);
}

int fl_fs_dir_open(const char *dir)
{
    int flags = O_RDONLY | O_CLOEXEC;

    if (!dir || !dir[0]) {
        errno = EINVAL;
        return -1;
    }
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    return open(dir, flags);
}

int fl_fs_write_file_atomic_in(int dirfd, const char *name, const uint8_t *data, size_t len)
{
#ifdef FL_FS_HAVE_OPENAT
    if (dirfd < 0) {
        errno = EBADF;
        return -1;
    }
    if (!leaf_name_ok(name))
        return -1;
    return write_atomic_at(dirfd, name, data, len);
#else
    (void)dirfd; (void)name; (void)data; (void)len;
    errno = ENOSYS;
    return -1;
#endif
}

int fl_fs_contained_available(void)
{
#ifdef FL_FS_HAVE_OPENAT
    return 1;
#else
    return 0;
#endif
}

#ifdef FL_FS_HAVE_OPENAT
#define IN_GUARD(...) do { if (dirfd < 0) { errno = EBADF; return -1; } \
    { const char *n_[] = {__VA_ARGS__}; size_t i_; \
      for (i_ = 0; i_ < sizeof n_ / sizeof *n_; i_++) if (!leaf_name_ok(n_[i_])) return -1; } } while (0)
#else
#define IN_GUARD(...) do { (void)dirfd; errno = ENOSYS; return -1; } while (0)
#endif

int fl_fs_stage_file_in(int dirfd, const char *name, const uint8_t *data, size_t len)
{
    IN_GUARD(name);
    return stage_at(dirfd, name, data, len);
}

int fl_fs_rename_in(int dirfd, const char *from, const char *to)
{
    IN_GUARD(from, to);
    return at_rename(dirfd, from, to);
}

int fl_fs_unlink_in(int dirfd, const char *name)
{
    IN_GUARD(name);
    return at_unlink(dirfd, name);
}

int fl_fs_link_in(int dirfd, const char *from, const char *to)
{
    IN_GUARD(from, to);
#ifdef FL_FS_HAVE_OPENAT
    return linkat(dirfd, from, dirfd, to, 0);
#endif
}

int fl_fs_exists_in(int dirfd, const char *name)
{
    struct stat st;

    IN_GUARD(name);
    if (at_lstat(dirfd, name, &st) == 0)
        return 1;
    return errno == ENOENT ? 0 : -1;
}

int fl_fs_open_read_in(int dirfd, const char *name)
{
    IN_GUARD(name);
    return at_open(dirfd, name, O_RDONLY | O_CLOEXEC | FL_FS_O_NOFOLLOW | O_NONBLOCK, 0);
}

/* The body of the readers below: takes ownership of fd and closes it. */
static int read_fd(int fd, size_t max, uint8_t **data, size_t *len)
{
    struct stat st;
    uint8_t *buf = NULL;
    uint8_t probe;
    size_t n, got = 0;
    ssize_t r;
    int rc = -1, saved_errno;

    if (fstat(fd, &st) != 0)
        goto out;
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        goto out;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)max ||
        (uint64_t)st.st_size > (uint64_t)SIZE_MAX) {
        errno = EFBIG;
        rc = -2;
        goto out;
    }
    n = (size_t)st.st_size;
    buf = (uint8_t *)malloc(n ? n : 1u); /* once: no realloc copies of the content */
    if (!buf)
        goto out;
    while (got < n) {
        r = read(fd, buf + got, n - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) /* error, or the file shrank */
            goto out;
        got += (size_t)r;
    }
    do {
        r = read(fd, &probe, 1);
    } while (r < 0 && errno == EINTR);
    if (r != 0) {
        if (r > 0) { /* the file grew: never hand back a truncated read */
            errno = EFBIG;
            rc = -2;
        }
        goto out;
    }
    *data = buf;
    *len = n;
    buf = NULL;
    rc = 0;
out:
    saved_errno = errno; /* the wipe, free and close must not replace the failure's errno */
    if (buf) {
        explicit_bzero(buf, got);
        free(buf);
    }
    close(fd);
    errno = saved_errno;
    return rc;
}

int fl_fs_read_file(const char *path, size_t max, uint8_t **data, size_t *len)
{
    int fd;

    if (!data || !len)
        return -1;
    *data = NULL;
    *len = 0;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    /* O_NONBLOCK: a FIFO must not hang the open; it is refused by fstat in read_fd. */
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return -3;
    return read_fd(fd, max, data, len);
}

int fl_fs_read_file_in(int dirfd, const char *name, size_t max, uint8_t **data, size_t *len)
{
#ifdef FL_FS_HAVE_OPENAT
    int fd;

    if (!data || !len)
        return -1;
    *data = NULL;
    *len = 0;
    if (dirfd < 0) {
        errno = EBADF;
        return -1;
    }
    if (!leaf_name_ok(name))
        return -1;
    fd = openat(dirfd, name, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0)
        return -3;
    return read_fd(fd, max, data, len);
#else
    (void)dirfd; (void)name; (void)max;
    if (data)
        *data = NULL;
    if (len)
        *len = 0;
    errno = ENOSYS;
    return -1;
#endif
}

int fl_fs_join_path(char *out, size_t cap, const char *a, const char *b)
{
    int r;

    if (!out || cap == 0u)
        return -1;
    if (!a || !a[0])
        r = snprintf(out, cap, "%s", b ? b : "");
    else if (a[strlen(a) - 1] == '/')
        r = snprintf(out, cap, "%s%s", a, b ? b : "");
    else
        r = snprintf(out, cap, "%s/%s", a, b ? b : "");
    if (r < 0 || (size_t)r >= cap) {
        errno = ENAMETOOLONG;
        return -ENAMETOOLONG;
    }
    return 0;
}

/* ---- landing directory and the all-or-nothing replace ---------------------- */

/* The directory the replace works in: an fd (contained), or fd -1 with the directory's path. */
typedef struct {
    int         fd;
    const char *dir;
} landing_t;

static int at_link(int dirfd, const char *from, const char *to)
{
#ifdef FL_FS_HAVE_OPENAT
    return linkat(dirfd, from, dirfd, to, 0);
#else
    (void)dirfd;
    return link(from, to);
#endif
}

/* Where name lives for the *at calls: inside the fd, or at its joined path from the cwd. */
static int landing_target(const landing_t *l, const char *name, char *buf, size_t cap, int *dirfd,
                          const char **target)
{
    if (l->fd >= 0) {
        *dirfd = l->fd;
        *target = name;
        return 0;
    }
    *dirfd = FL_FS_CWDFD;
    *target = buf;
    return fl_fs_join_path(buf, cap, l->dir, name) == 0 ? 0 : -1;
}

/* A landing is usable when the fd is open, or when there are no contained calls to use. */
static int landing_ready(int dirfd, const char *dir_path)
{
    if (dirfd >= 0)
        return fl_fs_contained_available();
    return !fl_fs_contained_available() && dir_path && dir_path[0];
}

int fl_fs_exists_at(int dirfd, const char *dir_path, const char *name)
{
    char p[FL_FS_PATH_MAX];

    if (dirfd >= 0)
        return fl_fs_exists_in(dirfd, name);
    if (fl_fs_contained_available()) {
        errno = EBADF;
        return -1;
    }
    if (!dir_path || !name || fl_fs_join_path(p, sizeof p, dir_path, name) != 0)
        return -1;
    return access(p, F_OK) == 0 ? 1 : 0;
}

int fl_fs_read_file_at(int dirfd, const char *dir_path, const char *name, size_t max,
                       uint8_t **data, size_t *len)
{
    char p[FL_FS_PATH_MAX];

    if (dirfd >= 0)
        return fl_fs_read_file_in(dirfd, name, max, data, len);
    if (data)
        *data = NULL;
    if (len)
        *len = 0;
    if (fl_fs_contained_available()) {
        errno = EBADF;
        return -1;
    }
    if (!dir_path || !name || fl_fs_join_path(p, sizeof p, dir_path, name) != 0)
        return -1;
    return fl_fs_read_file(p, max, data, len);
}

static int land_stage(const landing_t *l, const char *name, const uint8_t *d, size_t n)
{
    char buf[FL_FS_PATH_MAX];
    const char *t;
    int dfd;

    return landing_target(l, name, buf, sizeof buf, &dfd, &t) == 0 ? stage_at(dfd, t, d, n) : -1;
}

static int land_write(const landing_t *l, const char *name, const uint8_t *d, size_t n)
{
    char buf[FL_FS_PATH_MAX];
    const char *t;
    int dfd;

    return landing_target(l, name, buf, sizeof buf, &dfd, &t) == 0
               ? write_atomic_at(dfd, t, d, n)
               : -1;
}

static int land_unlink(const landing_t *l, const char *name)
{
    char buf[FL_FS_PATH_MAX];
    const char *t;
    int dfd;

    return landing_target(l, name, buf, sizeof buf, &dfd, &t) == 0 ? at_unlink(dfd, t) : -1;
}

static int land_rename(const landing_t *l, const char *from, const char *to)
{
    char a[FL_FS_PATH_MAX], b[FL_FS_PATH_MAX];
    const char *ta, *tb;
    int dfd;

    if (landing_target(l, from, a, sizeof a, &dfd, &ta) != 0 ||
        landing_target(l, to, b, sizeof b, &dfd, &tb) != 0)
        return -1;
    return at_rename(dfd, ta, tb);
}

static int land_link(const landing_t *l, const char *from, const char *to)
{
    char a[FL_FS_PATH_MAX], b[FL_FS_PATH_MAX];
    const char *ta, *tb;
    int dfd;

    if (landing_target(l, from, a, sizeof a, &dfd, &ta) != 0 ||
        landing_target(l, to, b, sizeof b, &dfd, &tb) != 0)
        return -1;
    return at_link(dfd, ta, tb);
}

/* The most a backup copy reads (a file past it is not backed up, so its replace is refused). */
#define FL_FS_COMMIT_COPY_MAX (4u * 1024u * 1024u)

/*
 * Keeps name as bak. A hard link costs nothing; a file system without links (the browser
 * lab's in-memory one refuses them) gets a copy instead, wiped from memory after.
 */
static int backup_file(const landing_t *l, const char *name, const char *bak)
{
    uint8_t *copy = NULL;
    size_t len = 0;
    int rc;

    if (land_link(l, name, bak) == 0)
        return 0;
    if (fl_fs_read_file_at(l->fd, l->dir, name, FL_FS_COMMIT_COPY_MAX, &copy, &len) != 0)
        return -1;
    rc = land_write(l, bak, copy, len);
    explicit_bzero(copy, len);
    free(copy);
    return rc;
}

/* 1 when a is b plus a suffix the replace adds to a name (a staging or backup name). */
static int is_sibling_of(const char *a, const char *b)
{
    size_t lb = strlen(b);

    return strncmp(a, b, lb) == 0 &&
           (strcmp(a + lb, FL_FS_PART_SUFFIX) == 0 || strcmp(a + lb, FL_FS_BACKUP_SUFFIX) == 0);
}

/* A plain name that leaves room for every sibling, the copied backup's .kqbak.part included. */
static int commit_name_ok(const char *name)
{
    if (!leaf_name_ok(name))
        return 0;
    if (strlen(name) > FL_FS_COMMIT_NAME_MAX) {
        errno = ENAMETOOLONG;
        return 0;
    }
    return 1;
}

int fl_fs_commit_files(int dirfd, const char *dir_path, const fl_fs_file_out_t *files, size_t n)
{
    const landing_t l = {dirfd, dir_path};
    char part[FL_FS_NAME_MAX + 1u];
    char bak[FL_FS_NAME_MAX + 1u];
    int had[FL_FS_COMMIT_FILES_MAX] = {0};
    size_t done = 0;
    size_t i, j;
    int rc = 0;
    int err = 0;
    int exist;

    if (!files || n == 0u || n > FL_FS_COMMIT_FILES_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (!landing_ready(dirfd, dir_path)) {
        errno = dirfd >= 0 ? ENOSYS : EBADF;
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (!files[i].name || (files[i].len && !files[i].data)) {
            errno = EINVAL;
            return -1;
        }
        if (!commit_name_ok(files[i].name))
            return -1;
        for (j = 0; j < i; j++) {
            if (strcmp(files[i].name, files[j].name) == 0 ||
                is_sibling_of(files[i].name, files[j].name) ||
                is_sibling_of(files[j].name, files[i].name)) {
                errno = EINVAL;
                return -1;
            }
        }
    }
    for (i = 0; i < n; i++) {
        if (land_stage(&l, files[i].name, files[i].data, files[i].len) != 0)
            break;
    }
    if (i < n) {
        err = errno;
        while (i-- > 0) {
            snprintf(part, sizeof part, "%s" FL_FS_PART_SUFFIX, files[i].name);
            land_unlink(&l, part);
        }
        errno = err;
        return -1;
    }
    for (i = 0; i < n; i++) {
        snprintf(bak, sizeof bak, "%s" FL_FS_BACKUP_SUFFIX, files[i].name);
        land_unlink(&l, bak);
        exist = fl_fs_exists_at(l.fd, l.dir, files[i].name);
        if (exist < 0) { /* cannot tell: fail closed, nothing has been replaced yet */
            err = errno;
            rc = -1;
            break;
        }
        had[i] = exist == 1;
        if (had[i] && backup_file(&l, files[i].name, bak) != 0) {
            err = errno;
            rc = -1;
            break;
        }
    }
    for (; rc == 0 && done < n; done++) {
        snprintf(part, sizeof part, "%s" FL_FS_PART_SUFFIX, files[done].name);
        if (land_rename(&l, part, files[done].name) != 0) {
            err = errno;
            rc = -1;
        }
    }
    if (rc != 0) {
        /* Put back what was replaced; a file that did not exist before is removed. */
        size_t replaced = done ? done - 1 : 0;
        for (i = 0; i < replaced; i++) {
            snprintf(bak, sizeof bak, "%s" FL_FS_BACKUP_SUFFIX, files[i].name);
            if (had[i])
                (void)land_rename(&l, bak, files[i].name);
            else
                land_unlink(&l, files[i].name);
        }
        for (i = 0; i < n; i++) {
            snprintf(part, sizeof part, "%s" FL_FS_PART_SUFFIX, files[i].name);
            land_unlink(&l, part);
        }
    }
    for (i = 0; i < n; i++) {
        snprintf(bak, sizeof bak, "%s" FL_FS_BACKUP_SUFFIX, files[i].name);
        land_unlink(&l, bak);
    }
    if (rc != 0)
        errno = err;
    return rc;
}

static fl_result_t read_meta_share_id(const char *meta_path,
                                      char *share_id,
                                      size_t share_id_cap)
{
    char line[256];
    FILE *fp;

    if (!meta_path || !share_id || share_id_cap == 0u)
        return FL_RESULT_INVAL;
    share_id[0] = '\0';
    fp = fopen(meta_path, "r");
    if (!fp)
        return FL_RESULT_NOENT;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "share_id=%63[^\n]", share_id) == 1)
            break;
    }
    fclose(fp);
    return share_id[0] ? FL_RESULT_OK : FL_RESULT_NOENT;
}

static int landing_path_exists(const char *root, const char *landed)
{
    char path[512];
    if (fl_fs_join_path(path, sizeof(path), root, landed) != 0)
        return 0;
    return access(path, F_OK) == 0;
}

static int landing_meta_exists(const char *root, const char *landed)
{
    char meta_path[512];
    int n;
    n = snprintf(meta_path, sizeof(meta_path), "%s/%s%s",
                 root, landed, FL_SERVER_SHARED_META_SUFFIX);
    if (n < 0 || (size_t)n >= sizeof(meta_path))
        return 0;
    return access(meta_path, F_OK) == 0;
}

/* ASCII-only, equal-length comparison ignoring case (a file system may fold case). */
static int ascii_casecmp(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;

        if (x >= 'A' && x <= 'Z')
            x = (unsigned char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z')
            y = (unsigned char)(y + ('a' - 'A'));
        if (x != y)
            return 1;
    }
    return *a != *b;
}

int fl_server_shared_name_is_reserved(const char *name)
{
    static const char *const reserved[] = {FL_FS_PART_SUFFIX, FL_SERVER_SHARED_META_SUFFIX,
                                           ".kqtf", ".kqseen", FL_FS_BACKUP_SUFFIX};
    size_t len, i;

    if (!name)
        return 0;
    len = strlen(name);
    for (i = 0; i < sizeof reserved / sizeof reserved[0]; i++) {
        size_t sl = strlen(reserved[i]);

        if (len >= sl && !ascii_casecmp(name + len - sl, reserved[i]))
            return 1;
    }
    return 0;
}

/** `n` 1 = base name; 2+ = `stem (n).ext` disambiguation without share id in the name. */
static fl_result_t landing_candidate_name(const char *file_name,
                                          unsigned n,
                                          char *out,
                                          size_t out_cap)
{
    const char *dot;
    int w;

    if (!fl_fs_name_is_plain(file_name) || fl_server_shared_name_is_reserved(file_name) || !out ||
        out_cap == 0u)
        return FL_RESULT_INVAL;
    if (n < 2u) {
        if (strlen(file_name) >= out_cap)
            return FL_RESULT_INVAL;
        strncpy(out, file_name, out_cap - 1u);
        out[out_cap - 1u] = '\0';
        return FL_RESULT_OK;
    }
    dot = strrchr(file_name, '.');
    if (dot && dot != file_name) {
        w = snprintf(out, out_cap, "%.*s (%u)%s", (int)(dot - file_name),
                     file_name, n, dot);
    } else {
        w = snprintf(out, out_cap, "%s (%u)", file_name, n);
    }
    if (w < 0 || (size_t)w >= out_cap)
        return FL_RESULT_INVAL;
    return fl_fs_name_is_plain(out) && !fl_server_shared_name_is_reserved(out) ? FL_RESULT_OK
                                                                              : FL_RESULT_INVAL;
}

static int landing_conflicts(const char *root,
                             const char *landed,
                             const char *share_id)
{
    char meta_path[512];
    char existing[FL_SERVER_SHARE_ID_MAX];
    int n;
    fl_result_t rc;

    if (!landing_path_exists(root, landed) && !landing_meta_exists(root, landed))
        return 0;
    n = snprintf(meta_path, sizeof(meta_path), "%s/%s%s",
                 root, landed, FL_SERVER_SHARED_META_SUFFIX);
    if (n < 0 || (size_t)n >= sizeof(meta_path))
        return 1;
    rc = read_meta_share_id(meta_path, existing, sizeof(existing));
    if (rc == FL_RESULT_OK && !strcmp(existing, share_id))
        return 0;
    return 1;
}

static fl_result_t resolve_unique_landed(const char *root,
                                         const fl_server_file_offer_t *offer,
                                         char *landed,
                                         size_t landed_cap)
{
    unsigned n;

    if (!root || !offer || !landed || landed_cap == 0u)
        return FL_RESULT_INVAL;
    for (n = 1u; n < 1000u; n++) {
        if (landing_candidate_name(offer->file_name, n, landed, landed_cap) !=
            FL_RESULT_OK)
            return FL_RESULT_INVAL;
        if (!landing_conflicts(root, landed, offer->share_id))
            return FL_RESULT_OK;
    }
    return FL_RESULT_INVAL;
}

static fl_result_t write_landing_sidecar(int root_fd,
                                         const char *landed,
                                         const fl_server_file_offer_t *offer)
{
    char meta_name[FL_SERVER_SHARED_LANDED_MAX + 8u];
    char body[512];
    int n, m;
    if (root_fd < 0 || !landed || !offer)
        return FL_RESULT_INVAL;
    n = snprintf(meta_name, sizeof(meta_name), "%s%s", landed, FL_SERVER_SHARED_META_SUFFIX);
    if (n < 0 || (size_t)n >= sizeof(meta_name))
        return FL_RESULT_INVAL;
    m = snprintf(body, sizeof(body), "share_id=%s\n", offer->share_id);
    if (offer->expires_at != 0u && m > 0 && (size_t)m < sizeof(body))
        m += snprintf(body + m, sizeof(body) - (size_t)m, "expires_at=%llu\n",
                      (unsigned long long)offer->expires_at);
    if (m > 0 && (size_t)m < sizeof(body))
        m += snprintf(body + m, sizeof(body) - (size_t)m, "file_name=%s\n", offer->file_name);
    if (m < 0 || (size_t)m >= sizeof(body))
        return FL_RESULT_INVAL;
    if (fl_fs_write_file_atomic_in(root_fd, meta_name, (const uint8_t *)body, (size_t)m) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t move_landed_to_expired(const char *root,
                                          const char *expired_root,
                                          const char *landed)
{
    char src_file[512];
    char dst_file[512];
    char src_meta[512];
    char dst_meta[512];

    if (fl_fs_join_path(src_file, sizeof(src_file), root, landed) != 0)
        return FL_RESULT_ERR;
    if (fl_fs_join_path(dst_file, sizeof(dst_file), expired_root, landed) != 0)
        return FL_RESULT_ERR;
    if (rename(src_file, dst_file) != 0 && errno != ENOENT)
        return FL_RESULT_ERR;
    if (snprintf(src_meta, sizeof(src_meta), "%s/%s%s",
                 root, landed, FL_SERVER_SHARED_META_SUFFIX) < 0 ||
        snprintf(dst_meta, sizeof(dst_meta), "%s/%s%s",
                 expired_root, landed, FL_SERVER_SHARED_META_SUFFIX) < 0)
        return FL_RESULT_ERR;
    if (rename(src_meta, dst_meta) != 0 && errno != ENOENT)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

/*
 * Lexical gate, then a walk of the directory components under g_cwd, one directory fd at a
 * time, each opened without following links. Nothing is resolved by name after the check,
 * so what is checked is what is used.
 */
#define FL_LOCAL_PATH_DEPTH_MAX 64u

fl_result_t fl_server_shared_resolve_local_path(const char *local_path,
                                                int create_parents,
                                                int *dir_fd,
                                                char *leaf,
                                                size_t leaf_cap)
{
#ifdef FL_FS_HAVE_OPENAT
    char joined[FL_FS_PATH_MAX];
    char buf[FL_FS_PATH_MAX];
    char *comp[FL_LOCAL_PATH_DEPTH_MAX];
    size_t n = 0, i, len;
    char *tok, *save = NULL;
    int cur;
    const char *base = g_cwd[0] ? g_cwd : ".";

    if (dir_fd)
        *dir_fd = -1;
    if (!local_path || !local_path[0] || !dir_fd || !leaf || leaf_cap == 0u)
        return FL_RESULT_INVAL;
    len = strlen(local_path);
    if (len >= sizeof(buf) || strlen(base) + 1u + len >= sizeof(joined))
        return FL_RESULT_INVAL;
    if (local_path[0] == '/' || strchr(local_path, '\\'))
        return FL_RESULT_ACCES;
    memcpy(buf, local_path, len + 1u);
    for (tok = strtok_r(buf, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (!strcmp(tok, "."))
            continue;
        if (!strcmp(tok, ".."))
            return FL_RESULT_ACCES;
        if (n == FL_LOCAL_PATH_DEPTH_MAX)
            return FL_RESULT_INVAL;
        comp[n++] = tok;
    }
    if (n == 0u || local_path[len - 1u] == '/')
        return FL_RESULT_INVAL;
    if (strlen(comp[n - 1u]) >= leaf_cap)
        return FL_RESULT_INVAL;
    /* The destination's file name follows the landing rules, so no caller can reach a name the
     * delivery keeps for its own files. */
    if (!fl_fs_name_is_plain(comp[n - 1u]) || fl_server_shared_name_is_reserved(comp[n - 1u]))
        return FL_RESULT_ACCES;
    /* The gate sees the validated components, so every spelling of one place is gated alike. */
    {
        size_t w = (size_t)snprintf(joined, sizeof(joined), "%s", base);

        for (i = 0; i < n && w < sizeof(joined); i++)
            w += (size_t)snprintf(joined + w, sizeof(joined) - w, "/%s", comp[i]);
        if (w >= sizeof(joined))
            return FL_RESULT_INVAL;
    }
    if (fl_server_shared_path_is_expired_quarantine(joined))
        return FL_RESULT_ACCES;
    cur = fl_fs_dir_open(base);
    if (cur < 0)
        return FL_RESULT_ERR;
    for (i = 0; i + 1u < n; i++) {
        int next = openat(cur, comp[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

        if (next < 0 && errno == ENOENT && create_parents &&
            (mkdirat(cur, comp[i], 0700) == 0 || errno == EEXIST))
            next = openat(cur, comp[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            int e = errno;

            close(cur);
            return (e == ELOOP || e == ENOTDIR) ? FL_RESULT_ACCES : FL_RESULT_ERR;
        }
        close(cur);
        cur = next;
    }
    memcpy(leaf, comp[n - 1u], strlen(comp[n - 1u]) + 1u);
    *dir_fd = cur;
    return FL_RESULT_OK;
#else
    (void)local_path; (void)create_parents; (void)leaf; (void)leaf_cap;
    if (dir_fd)
        *dir_fd = -1;
    return FL_RESULT_ERR;
#endif
}

fl_result_t fl_server_shared_init(void)
{
    char root[512];
    char expired[512];
    if (fl_fs_join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (fl_fs_join_path(expired, sizeof(expired), root, FL_SERVER_SHARED_EXPIRED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (fl_fs_mkdir_p(root, 0700) != 0)
        return FL_RESULT_ERR;
    if (fl_fs_mkdir_p(expired, 0700) != 0)
        return FL_RESULT_ERR;
    (void)fl_server_catalog_open();
    return FL_RESULT_OK;
}

static int path_has_expired_component(const char *canon)
{
    const char *needle = FL_SERVER_SHARED_DIR_NAME "/" FL_SERVER_SHARED_EXPIRED_DIR_NAME;
    if (!canon)
        return 0;
    return strstr(canon, needle) != NULL;
}

int fl_server_shared_path_is_expired_quarantine(const char *path)
{
    char ab[4096];
    char rbuf[4096];
    if (!path || !path[0])
        return 0;
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(ab))
            return 0;
        strncpy(ab, path, sizeof(ab) - 1u);
    } else {
        if (snprintf(ab, sizeof(ab), "%s/%s", g_cwd, path) < 0)
            return 0;
    }
    ab[sizeof(ab) - 1u] = '\0';
    if (realpath(ab, rbuf) != NULL)
        return path_has_expired_component(rbuf);
    return path_has_expired_component(ab);
}

fl_result_t fl_server_shared_landed_basename(const fl_server_file_offer_t *offer,
                                             char *out,
                                             size_t out_cap)
{
    if (!offer || !out || out_cap == 0u || !offer->share_id[0] || !offer->file_name[0])
        return FL_RESULT_INVAL;
    if (!fl_fs_name_is_plain(offer->file_name) || fl_server_shared_name_is_reserved(offer->file_name))
        return FL_RESULT_INVAL;
    if (strlen(offer->file_name) >= out_cap)
        return FL_RESULT_INVAL;
    strncpy(out, offer->file_name, out_cap - 1u);
    out[out_cap - 1u] = '\0';
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_save_offer(const fl_server_file_offer_t *offer,
                                        const uint8_t *data,
                                        size_t data_len,
                                        char *out_path,
                                        size_t out_path_cap)
{
    char root[512];
    char landed[FL_SERVER_SHARED_LANDED_MAX];
    char file_path[512];
    int root_fd;
    fl_result_t rc;
    if (!offer || !offer->share_id[0] || !fl_fs_name_is_plain(offer->file_name) ||
        fl_server_shared_name_is_reserved(offer->file_name))
        return FL_RESULT_INVAL;
    if (fl_server_shared_init() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_fs_join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    rc = resolve_unique_landed(root, offer, landed, sizeof(landed));
    if (rc != FL_RESULT_OK)
        return rc;
    if (fl_fs_join_path(file_path, sizeof(file_path), root, landed) != 0)
        return FL_RESULT_ERR;
    root_fd = fl_fs_dir_open(root);
    if (root_fd < 0)
        return FL_RESULT_ERR;
    if (fl_fs_write_file_atomic_in(root_fd, landed, data, data_len) != 0) {
        close(root_fd);
        return FL_RESULT_ERR;
    }
    rc = write_landing_sidecar(root_fd, landed, offer);
    close(root_fd);
    if (rc != FL_RESULT_OK) {
        (void)remove(file_path);
        return rc;
    }
    if (out_path && out_path_cap > 0u) {
        strncpy(out_path, file_path, out_path_cap - 1u);
        out_path[out_path_cap - 1u] = '\0';
    }
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_overwrite_local(const fl_server_file_offer_t *offer,
                                             const char *local_path,
                                             const uint8_t *data,
                                             size_t data_len)
{
#ifdef FL_FS_HAVE_OPENAT
    char leaf[256];
    struct stat st;
    int dfd = -1;
    fl_result_t rc;

    (void)offer;
    if (!local_path || !local_path[0] || (data_len && !data))
        return FL_RESULT_INVAL;
    rc = fl_server_shared_resolve_local_path(local_path, 1, &dfd, leaf, sizeof(leaf));
    if (rc != FL_RESULT_OK)
        return rc;
    /*
     * The write stages a new owner-only (0600) file and renames it over the leaf, so the old
     * content stays whole if the write fails and a leaf with other names is replaced, not
     * written through. The rename never follows the name. The checks below therefore only
     * pick the return code and keep the old rule (a leaf the caller could not write is
     * refused); they are not the safety gate. A missing leaf is created.
     */
    if (fstatat(dfd, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(st.st_mode)) {
            close(dfd);
            return FL_RESULT_ACCES;
        }
#ifdef AT_EACCESS
        if (faccessat(dfd, leaf, W_OK, AT_EACCESS) != 0) {
#else
        /* no AT_EACCESS: the real ids are checked instead, the same where they match */
        if (faccessat(dfd, leaf, W_OK, 0) != 0) {
#endif
            close(dfd);
            return FL_RESULT_ACCES;
        }
    } else if (errno != ENOENT) {
        close(dfd);
        return FL_RESULT_ERR;
    }
    rc = fl_fs_write_file_atomic_in(dfd, leaf, data, data_len) == 0 ? FL_RESULT_OK
                                                                     : FL_RESULT_ERR;
    close(dfd);
    return rc;
#else
    (void)offer; (void)local_path; (void)data; (void)data_len;
    return FL_RESULT_ERR;
#endif
}

fl_result_t fl_server_shared_purge_expired(uint64_t now)
{
    char root[512];
    char expired_root[512];
    char meta_path[512];
    DIR *d;
    struct dirent *ent;
    size_t suffix_len = strlen(FL_SERVER_SHARED_META_SUFFIX);

    if (fl_server_shared_init() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_fs_join_path(root, sizeof(root), ".", FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (fl_fs_join_path(expired_root, sizeof(expired_root), root,
                  FL_SERVER_SHARED_EXPIRED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (fl_fs_mkdir_p(expired_root, 0700) != 0 && errno != EEXIST)
        return FL_RESULT_ERR;
    d = opendir(root);
    if (!d)
        return FL_RESULT_ERR;
    while ((ent = readdir(d)) != NULL) {
        char line[256];
        char landed[FL_SERVER_SHARED_LANDED_MAX];
        uint64_t expires_at = 0u;
        size_t name_len;
        FILE *fp;
        if (ent->d_name[0] == '.')
            continue;
        if (!strcmp(ent->d_name, FL_SERVER_SHARED_EXPIRED_DIR_NAME))
            continue;
        name_len = strlen(ent->d_name);
        if (name_len <= suffix_len ||
            strcmp(ent->d_name + name_len - suffix_len,
                   FL_SERVER_SHARED_META_SUFFIX) != 0)
            continue;
        if (name_len - suffix_len >= sizeof(landed))
            continue;
        memcpy(landed, ent->d_name, name_len - suffix_len);
        landed[name_len - suffix_len] = '\0';
        fl_fs_join_path(meta_path, sizeof(meta_path), root, ent->d_name);
        fp = fopen(meta_path, "r");
        if (!fp)
            continue;
        expires_at = 0u;
        while (fgets(line, sizeof(line), fp)) {
            unsigned long long v;
            if (sscanf(line, "expires_at=%llu", &v) == 1)
                expires_at = (uint64_t)v;
        }
        fclose(fp);
        if (expires_at != 0u && now > expires_at)
            (void)move_landed_to_expired(root, expired_root, landed);
    }
    closedir(d);
    (void)fl_server_catalog_purge_expired(now);
    return FL_RESULT_OK;
}
