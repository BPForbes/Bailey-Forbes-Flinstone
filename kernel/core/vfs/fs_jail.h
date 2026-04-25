/**
 * In VM / -Virtualization mode, restrict the local file provider to a single
 * host directory tree (launch cwd or the temp VM sandbox) so the shell does not
 * read or write outside that root.
 */
#ifndef FS_JAIL_H
#define FS_JAIL_H

#include <stddef.h>
#include <sys/types.h>

void fs_jail_init(void);
int  fs_jail_is_active(void);
int  fs_jail_root_configured(void);
/** Return 0 if the path (absolute or relative to g_cwd) may be accessed, -1 if outside jail. */
int  fs_jail_check_path(const char *path);
/** Atomically open a file within the jail, preventing TOCTOU symlink races. */
int  fs_jail_openat(const char *path, int flags, mode_t mode);

#endif /* FS_JAIL_H */
