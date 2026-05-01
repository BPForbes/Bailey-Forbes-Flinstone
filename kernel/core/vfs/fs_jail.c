#include "fs_jail.h"
#include "common.h"
#include "mem_domain.h"
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
    g_jail_dirfd = -1;
    if (!g_vm_mode)
        return;
    char wd[JAIL_ROOT_MAX];
    mem_domain_zero(wd, sizeof(wd));
    if (!getcwd(wd, sizeof(wd)))
        return;
    if (!g_vm_root[0]) {
        fprintf(stderr, "[VM] 5-layer driver config warning: layer 4 shell/VM root is not configured\n");
        fprintf(stderr, "[VM] 5-layer driver config warning: layer 2 filesystem sandbox root is not configured\n");
        return;
    }
    const char *root = g_vm_root;
    if (realpath(root, g_fs_jail_root) == NULL) {
        fprintf(stderr, "VM: sandbox root unavailable: %s\n", root);
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
    }
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

int fs_jail_check_path(const char *path) {
    if (!path)
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
    if (fs_jail_check_path(path) != 0) {
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

    /* Verify the opened file is within jail by checking device/inode */
    struct stat jail_st, file_st;
    if (fstat(g_jail_dirfd, &jail_st) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    if (fstat(fd, &file_st) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    /* Verify file is on same device as jail root */
    if (file_st.st_dev != jail_st.st_dev) {
        close(fd);
        errno = EPERM;
        return -1;
    }

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
