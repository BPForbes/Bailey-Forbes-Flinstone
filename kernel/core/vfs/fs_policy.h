#ifndef FS_POLICY_H
#define FS_POLICY_H

#include "fs_types.h"

#define FS_POLICY_MAX_PROTECTED 16

/* Protected paths policy - deny delete/write on certain paths (CODEOWNERS-style) */
typedef struct fs_protected_policy {
    fs_access_policy_t base;
    char protected[FS_POLICY_MAX_PROTECTED][FS_PATH_MAX];
    int count;
} fs_protected_policy_t;

fs_access_policy_t *fs_protected_policy_create(void);
void fs_protected_policy_add(fs_protected_policy_t *p, const char *path);
void fs_protected_policy_destroy(fs_access_policy_t *p);

#endif /* FS_POLICY_H */
