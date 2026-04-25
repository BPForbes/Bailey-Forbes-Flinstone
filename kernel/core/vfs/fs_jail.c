#include "fs_jail.h"
#include "common.h"
#include "mem_domain.h"
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define JAIL_ROOT_MAX 4096
static char   g_fs_jail_root[JAIL_ROOT_MAX];
static size_t g_fs_jail_len;

void fs_jail_init(void) {
    mem_domain_zero(g_fs_jail_root, sizeof(g_fs_jail_root));
    g_fs_jail_len = 0;
    if (!g_vm_mode)
        return;
    char wd[JAIL_ROOT_MAX];
    mem_domain_zero(wd, sizeof(wd));
    if (!getcwd(wd, sizeof(wd)))
        return;
    const char *root = g_vm_root[0] ? g_vm_root : wd;
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
}

int fs_jail_is_active(void) {
    return (g_vm_mode && g_fs_jail_len > 0) ? 1 : 0;
}

static int under_jail(const char *canon) {
    if (g_fs_jail_len == 0) return 1;
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
        strncpy(ab, path, sizeof(ab) - 1);
    } else {
        snprintf(ab, sizeof(ab), "%s/%s", g_cwd, path);
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
    char *dname = dirname(pcopy);
    char *dcan = realpath(dname, NULL);
    if (!dcan)
        return -1;
    int ok = under_jail(dcan) ? 0 : -1;
    free(dcan);
    return ok;
}
