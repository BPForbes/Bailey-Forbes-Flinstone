/**
 * In VM / -Virtualization mode, restrict the local file provider to a single
 * host directory tree under the VM launch root (or temp sandbox): a dedicated
 * subdirectory (FS_JAIL_HOST_SUBDIR) is created one level below that root so
 * repository files next to the shell are never inside the jail.
 */
#ifndef FS_JAIL_H
#define FS_JAIL_H

#include "contract_p0_fs_jail.h"
#include <stddef.h>
#include <sys/types.h>

/** Host directory created under g_vm_root / cwd; actual jail root is inside this. */
#define FS_JAIL_HOST_SUBDIR FL_CONTRACT_P0_FS_JAIL_HOST_SUBDIR

void fs_jail_init(void);
int  fs_jail_is_active(void);
int  fs_jail_root_configured(void);
/** Return 0 if the path (absolute or relative to g_cwd) may be accessed, -1 if outside jail. */
int  fs_jail_check_path(const char *path);
/** Jail + path property: 0 if allowed, -1 if outside jail or elevation required but missing. */
int  fs_jail_check_access(const char *path);
/**
 * Account homes (contract_p2_heritage.h): <jail>/home/<key id segments after Root>.
 * Writes the absolute home of key_id into out and creates it (owner-only) when missing.
 * -1 when the jail is inactive, the key id is invalid or the path cannot be made.
 */
int  fs_jail_account_home(const char *key_id, char *out, size_t cap);
/**
 * Heritage check for a path inside the jail: 0 when the logged-in account may use it.
 * Inside <jail>/home the owner is the deepest account whose home holds the path, and the
 * logged-in account must be that owner, an ancestor of it, or joined to it by an active
 * bridge. Paths outside the home tree, and every path while the jail is inactive, pass.
 */
int  fs_jail_check_home(const char *path);
/** Atomically open a file within the jail, preventing TOCTOU symlink races. */
int  fs_jail_openat(const char *path, int flags, mode_t mode);

#endif /* FS_JAIL_H */
