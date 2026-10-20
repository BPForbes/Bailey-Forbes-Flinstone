#include "fs_jail.h"
#include "common.h"
#include "mem_domain.h"
#include "server_shared_fs.h"
#include "../identity/session.h"
#include "../identity/path_property.h"
#include "../identity/heritage.h"
#include "../identity/bridge.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define JAIL_ROOT_MAX 4096
char   g_fs_jail_root[JAIL_ROOT_MAX];
static size_t g_fs_jail_len;
static int    g_jail_dirfd = -1;

void fs_jail_init(void) {
    mem_domain_zero(g_fs_jail_root, sizeof(g_fs_jail_root));
    g_fs_jail_len = 0;
    if (g_jail_dirfd >= 0) {
        close(g_jail_dirfd);
        g_jail_dirfd = -1;
    }
    if (!g_vm_mode)
        return;
    char wd[JAIL_ROOT_MAX];
    mem_domain_zero(wd, sizeof(wd));
    if (!getcwd(wd, sizeof(wd)))
        return;
    const char *root = g_vm_root[0] ? g_vm_root : wd;
    char base[JAIL_ROOT_MAX];
    mem_domain_zero(base, sizeof(base));
    if (realpath(root, base) == NULL) {
        fprintf(stderr, "VM: sandbox root unavailable: %s\n", root);
        return;
    }
    size_t blen = strlen(base);
    while (blen > 1 && base[blen - 1] == '/')
        base[--blen] = '\0';

    char inner[JAIL_ROOT_MAX + 32];
    int n = snprintf(inner, sizeof(inner), "%s/%s", base, FS_JAIL_HOST_SUBDIR);
    if (n < 0 || (size_t)n >= sizeof(inner)) {
        fprintf(stderr, "VM: sandbox path too long (under %s)\n", base);
        return;
    }
    if (mkdir(inner, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "VM: cannot create host filesystem sandbox %s: %s\n", inner,
                strerror(errno));
        return;
    }
    if (realpath(inner, g_fs_jail_root) == NULL) {
        fprintf(stderr, "VM: cannot resolve host filesystem sandbox %s\n", inner);
        mem_domain_zero(g_fs_jail_root, sizeof(g_fs_jail_root));
        return;
    }
    g_fs_jail_root[JAIL_ROOT_MAX - 1] = '\0';
    g_fs_jail_len = strlen(g_fs_jail_root);
    while (g_fs_jail_len > 1 && g_fs_jail_root[g_fs_jail_len - 1] == '/') {
        g_fs_jail_root[--g_fs_jail_len] = '\0';
    }
    /* Open jail root directory for atomic openat operations */
    g_jail_dirfd = open(g_fs_jail_root, O_RDONLY | O_DIRECTORY);
    if (g_jail_dirfd < 0) {
        fprintf(stderr, "VM: failed to open jail root directory: %s\n", g_fs_jail_root);
        mem_domain_zero(g_fs_jail_root, sizeof(g_fs_jail_root));
        g_fs_jail_len = 0;
        return;
    }
    /* Host cwd and logical g_cwd match the jail so relative paths cannot reach repo files. */
    if (chdir(g_fs_jail_root) != 0) {
        fprintf(stderr, "VM: cannot chdir into sandbox %s: %s\n", g_fs_jail_root, strerror(errno));
        close(g_jail_dirfd);
        g_jail_dirfd = -1;
        mem_domain_zero(g_fs_jail_root, sizeof(g_fs_jail_root));
        g_fs_jail_len = 0;
        return;
    }
    if (g_fs_jail_len >= sizeof(g_cwd)) {
        fprintf(stderr, "VM: sandbox path too long for g_cwd (%zu >= %zu)\n",
                (size_t)g_fs_jail_len, sizeof(g_cwd));
        close(g_jail_dirfd);
        g_jail_dirfd = -1;
        mem_domain_zero(g_fs_jail_root, sizeof(g_fs_jail_root));
        g_fs_jail_len = 0;
        return;
    }
    memcpy(g_cwd, g_fs_jail_root, g_fs_jail_len + 1);
}

int fs_jail_is_active(void) {
    return (g_vm_mode && g_fs_jail_len > 0) ? 1 : 0;
}

int fs_jail_root_configured(void) {
    return (g_vm_root[0] && g_fs_jail_len > 0 && g_jail_dirfd >= 0) ? 1 : 0;
}

static int under_jail(const char *canon) {
    if (strncmp(canon, g_fs_jail_root, g_fs_jail_len) != 0) return 0;
    return (canon[g_fs_jail_len] == '\0' || canon[g_fs_jail_len] == '/');
}

/* Absolute form of path (relative to g_cwd). */
static int jail_absolute(const char *path, char *ab, size_t cap) {
    if (path[0] == '/') {
        if (strlen(path) >= cap)
            return -1;
        strncpy(ab, path, cap - 1);
    } else {
        int n = snprintf(ab, cap, "%s/%s", g_cwd, path);
        if (n < 0 || (size_t)n >= cap)
            return -1;
    }
    ab[cap - 1] = '\0';
    return 0;
}

/*
 * Canonical path: realpath when it exists, else the realpath of its deepest existing
 * ancestor followed by the remaining components (so a file about to be created is
 * judged by where it would land).
 */
static int jail_canonical(const char *path, char *out, size_t cap) {
    char ab[JAIL_ROOT_MAX * 2];
    char pcopy[JAIL_ROOT_MAX * 2];
    char *dcan;

    mem_domain_zero(ab, sizeof(ab));
    if (!path || !path[0] || jail_absolute(path, ab, sizeof(ab)) != 0)
        return -1;
    if (realpath(ab, out) != NULL && strlen(out) < cap)
        return 0;
    strncpy(pcopy, ab, sizeof(pcopy) - 1);
    pcopy[sizeof(pcopy) - 1] = '\0';
    for (;;) {
        char *slash = strrchr(pcopy, '/');
        int n;

        if (!slash || slash == pcopy)
            return -1;
        *slash = '\0';
        dcan = realpath(pcopy, NULL);
        if (!dcan)
            continue;
        n = snprintf(out, cap, "%s/%s", dcan, ab + strlen(pcopy) + 1);
        free(dcan);
        return (n < 0 || (size_t)n >= cap) ? -1 : 0;
    }
}

static int path_under(const char *canon, const char *dir) {
    size_t len = strlen(dir);

    if (strncmp(canon, dir, len) != 0)
        return 0;
    return canon[len] == '\0' || canon[len] == '/';
}

int fs_jail_account_home(const char *key_id, char *out, size_t cap) {
    char rel[FL_HERITAGE_KEY_ID_MAX + 8];
    char *p;
    int n;

    if (!out || cap == 0u)
        return -1;
    out[0] = '\0';
    if (!fs_jail_is_active() || fl_heritage_home_rel(key_id, rel, sizeof rel) != FL_RESULT_OK)
        return -1;
    n = snprintf(out, cap, "%s/%s", g_fs_jail_root, rel);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return -1;
    }
    /* mkdir -p below the jail root, one component at a time. */
    for (p = out + g_fs_jail_len + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char saved = *p;

            *p = '\0';
            if (mkdir(out, 0700) != 0 && errno != EEXIST) {
                *p = saved;
                return -1;
            }
            *p = saved;
            if (saved == '\0')
                break;
        }
    }
    return 0;
}

int fs_jail_check_home(const char *path) {
    char canon[JAIL_ROOT_MAX * 2];
    char home_root[JAIL_ROOT_MAX + 16];
    char best_home[JAIL_ROOT_MAX * 2];
    const char *owner_key = FL_HERITAGE_ROOT_KEY;
    const char *actor_key;
    const fl_user_db_t *db;
    size_t i;
    int n;

    if (!fs_jail_is_active())
        return 0;
    n = snprintf(home_root, sizeof home_root, "%s/%s", g_fs_jail_root, FL_HERITAGE_HOME_DIR);
    if (n < 0 || (size_t)n >= sizeof home_root)
        return -1;
    if (jail_canonical(path, canon, sizeof canon) != 0) {
        /* Nothing on the path exists: judge the literal absolute path. */
        mem_domain_zero(canon, sizeof canon);
        if (!path || !path[0] || jail_absolute(path, canon, sizeof canon) != 0)
            return -1;
        return path_under(canon, home_root) ? -1 : 0;
    }
    if (!path_under(canon, home_root))
        return 0;

    /* The owner is the deepest account whose home holds the path; the home tree itself is Root's. */
    fl_session_init();
    db = fl_session_user_db();
    best_home[0] = '\0';
    for (i = 0; db && i < db->count; i++) {
        char rel[FL_HERITAGE_KEY_ID_MAX + 8];
        char home[JAIL_ROOT_MAX * 2];

        if (fl_heritage_home_rel(db->users[i].key_id, rel, sizeof rel) != FL_RESULT_OK)
            continue;
        n = snprintf(home, sizeof home, "%s/%s", g_fs_jail_root, rel);
        if (n < 0 || (size_t)n >= sizeof home)
            continue;
        if (path_under(canon, home) && strlen(home) > strlen(best_home)) {
            strncpy(best_home, home, sizeof(best_home) - 1);
            best_home[sizeof(best_home) - 1] = '\0';
            owner_key = db->users[i].key_id;
        }
    }

    actor_key = fl_session_current_key_id();
    if (!actor_key[0])
        return -1;
    if (fl_heritage_is_ancestor_or_self(actor_key, owner_key))
        return 0;
    if (db && fl_bridge_active_between(db, actor_key, owner_key))
        return 0;
    return -1;
}

int fs_jail_check_access(const char *path) {
    fl_path_property_t prop;
    int jail_ok;

    if (fl_server_shared_path_is_expired_quarantine(path))
        return -1;

    if (!fs_jail_is_active())
        return 0;

    jail_ok = fs_jail_check_path(path);
    if (jail_ok != 0 && fl_session_jail_privileged())
        jail_ok = 0;

    if (jail_ok != 0)
        return -1;
    /* Heritage is not lifted by sudo: another branch's home needs a bridge. */
    if (fs_jail_check_home(path) != 0)
        return -1;

    {
        fl_result_t prop_rc = fl_path_property_resolve(path, &prop);
        if (prop_rc == FL_RESULT_NOENT)
            return 0;
        if (prop_rc != FL_RESULT_OK)
            return -1;
    }
    if (!prop.requires_elevation)
        return 0;
    fl_session_init();
    if (fl_session_has_elevation())
        return 0;
    return -1;
}

int fs_jail_check_path(const char *path) {
    if (!path)
        return -1;
    if (fl_server_shared_path_is_expired_quarantine(path))
        return -1;
    if (!fs_jail_is_active())
        return 0;
    if (!path[0])
        return -1;

    char ab[JAIL_ROOT_MAX * 2];
    mem_domain_zero(ab, sizeof(ab));
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(ab))
            return -1;
        strncpy(ab, path, sizeof(ab) - 1);
    } else {
        int n = snprintf(ab, sizeof(ab), "%s/%s", g_cwd, path);
        if (n < 0 || (size_t)n >= sizeof(ab))
            return -1;
    }
    ab[sizeof(ab) - 1] = '\0';

    char rbuf[JAIL_ROOT_MAX * 2];
    mem_domain_zero(rbuf, sizeof(rbuf));
    if (realpath(ab, rbuf) != NULL)
        return under_jail(rbuf) ? 0 : -1;

    char pcopy[JAIL_ROOT_MAX * 2];
    mem_domain_zero(pcopy, sizeof(pcopy));
    strncpy(pcopy, ab, sizeof(pcopy) - 1);
    pcopy[sizeof(pcopy) - 1] = '\0';
    for (;;) {
        char *slash = strrchr(pcopy, '/');
        if (!slash || slash == pcopy)
            return -1;
        *slash = '\0';
        char *dcan = realpath(pcopy, NULL);
        if (dcan) {
            int ok = under_jail(dcan) ? 0 : -1;
            free(dcan);
            return ok;
        }
    }
}

int fs_jail_openat(const char *path, int flags, mode_t mode) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if (fl_server_shared_path_is_expired_quarantine(path)) {
        errno = EPERM;
        return -1;
    }

    /* If jail not active, fall back to regular open */
    if (!fs_jail_is_active() || g_jail_dirfd < 0) {
        return open(path, flags, mode);
    }

    /* Construct absolute path for jail check */
    char ab[JAIL_ROOT_MAX * 2];
    mem_domain_zero(ab, sizeof(ab));
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(ab)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(ab, path, sizeof(ab) - 1);
    } else {
        int n = snprintf(ab, sizeof(ab), "%s/%s", g_cwd, path);
        if (n < 0 || (size_t)n >= sizeof(ab)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    ab[sizeof(ab) - 1] = '\0';

    /* Check if path is under jail - this is a best-effort check */
    if (fs_jail_check_access(path) != 0) {
        errno = EPERM;
        return -1;
    }

    /* Compute relative path from jail root */
    const char *relpath = ab;
    if (strncmp(ab, g_fs_jail_root, g_fs_jail_len) == 0) {
        relpath = ab + g_fs_jail_len;
        while (*relpath == '/')
            relpath++;
        if (*relpath == '\0')
            relpath = ".";
    }

    /* Open with O_NOFOLLOW to prevent symlink attacks */
    int fd = openat(g_jail_dirfd, relpath, flags | O_NOFOLLOW, mode);
    if (fd < 0)
        return -1;

    /* Additional check: get canonical path of opened fd and verify it's under jail */
    char fd_path[JAIL_ROOT_MAX];
    char link_path[64];
    snprintf(link_path, sizeof(link_path), "/proc/self/fd/%d", fd);
    ssize_t len = readlink(link_path, fd_path, sizeof(fd_path) - 1);
    if (len > 0) {
        fd_path[len] = '\0';
        if (!under_jail(fd_path)) {
            close(fd);
            errno = EPERM;
            return -1;
        }
    }

    return fd;
}
