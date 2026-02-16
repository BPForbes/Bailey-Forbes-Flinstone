#include "fs_policy.h"
#include <stdlib.h>
#include <string.h>

static int protected_can_read(fs_access_policy_t *pol, const char *user, const char *path) {
    (void)user; (void)path;
    fs_protected_policy_t *p = (fs_protected_policy_t *)pol;
    for (int i = 0; i < p->count; i++) {
        if (strncmp(path, p->protected[i], strlen(p->protected[i])) == 0)
            return 1;  /* read allowed even on protected */
    }
    return 1;
}

static int protected_can_write(fs_access_policy_t *pol, const char *user, const char *path) {
    (void)user;
    fs_protected_policy_t *p = (fs_protected_policy_t *)pol;
    for (int i = 0; i < p->count; i++) {
        size_t len = strlen(p->protected[i]);
        if (strncmp(path, p->protected[i], len) == 0 &&
            (path[len] == '\0' || path[len] == '/'))
            return 0;  /* deny write on protected */
    }
    return 1;
}

static int protected_can_delete(fs_access_policy_t *pol, const char *user, const char *path) {
    return protected_can_write(pol, user, path);
}

static const fs_access_policy_vtable_t protected_vtable = {
    protected_can_read, protected_can_write, protected_can_delete
};

fs_access_policy_t *fs_protected_policy_create(void) {
    fs_protected_policy_t *p = calloc(1, sizeof(*p));
    if (p) p->base.vtable = &protected_vtable;
    return &p->base;
}

void fs_protected_policy_add(fs_protected_policy_t *p, const char *path) {
    if (p->count >= FS_POLICY_MAX_PROTECTED) return;
    strncpy(p->protected[p->count], path, FS_PATH_MAX - 1);
    p->count++;
}

void fs_protected_policy_destroy(fs_access_policy_t *p) {
    free(p);
}
