/* P5-1 VFS layer (module contract, normative). Mount table ownership, vnode ids, path walk
 * and symlink limits; fallible ops use fl_result_t (P0-2). See docs/ROADMAP.md Phase 5. */
#ifndef FL_CONTRACT_P5_VFS_H
#define FL_CONTRACT_P5_VFS_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P5_1_VFS_CONTRACT_DEFINED 1

/** Opaque stable id for an open file description / vnode interchange surface. */
typedef uint64_t fl_vnode_id_t;

/** Invalid vnode / file handle sentinel (must not alias a live id). */
#define FL_VNODE_ID_INVALID ((fl_vnode_id_t)0u)

/** Maximum symlink resolutions per single logical lookup (POSIX-shaped ELOOP guard). */
#define FL_VFS_SYMLOOP_MAX 40

/** Maximum path components (slash-separated) per walk including trailing NUL budget. */
#define FL_VFS_PATH_COMPONENTS_MAX 256u

/** Soft cap on simultaneous mount entries in lab builds (policy hook for reviews). */
#define FL_VFS_MOUNT_TABLE_MAX_ENTRIES 32u

/** Soft cap on open file descriptions / FD table entries (userland/kernel interchange). */
#define FL_VFS_OPEN_FD_MAX 256u

/** Max name bytes per **readdir** entry (excluding terminating NUL). */
#define FL_VFS_DIRENT_NAME_MAX_CHARS 255u

/** File type vocabulary for stat-like metadata interchange (distinct from vfs_type_t impl). */
typedef enum {
    FL_VFS_FILE_TYPE_UNKNOWN = 0,
    FL_VFS_FILE_TYPE_REGULAR = 1,
    FL_VFS_FILE_TYPE_DIRECTORY = 2,
    FL_VFS_FILE_TYPE_SYMLINK = 3,
    FL_VFS_FILE_TYPE_DEVICE = 4
} fl_vfs_file_type_t;

/** VFS permission bits for open and access paths (distinct from P2 authz proof). */
typedef uint32_t fl_vfs_mode_t;

#define FL_VFS_MODE_IRUSR 0400u
#define FL_VFS_MODE_IWUSR 0200u
#define FL_VFS_MODE_IXUSR 0100u
#define FL_VFS_MODE_IRGRP 0040u
#define FL_VFS_MODE_IWGRP 0020u
#define FL_VFS_MODE_IXGRP 0010u
#define FL_VFS_MODE_IROTH 0004u
#define FL_VFS_MODE_IWOTH 0002u
#define FL_VFS_MODE_IXOTH 0001u

typedef enum {
    FL_VFS_MOUNT_FLAG_NONE = 0,
    FL_VFS_MOUNT_FLAG_RDONLY = 1,
    FL_VFS_MOUNT_FLAG_NOSUID = 2,
    FL_VFS_MOUNT_FLAG_NOEXEC = 4
} fl_vfs_mount_flags_t;

/** Durability class for fsync-class VFS obligations (see also P6-3 persistent-log fsync). */
typedef enum {
    FL_VFS_DURABILITY_METADATA = 0,
    FL_VFS_DURABILITY_DATA = 1,
    FL_VFS_DURABILITY_FULL = 2
} fl_vfs_durability_class_t;

_Static_assert(FL_VFS_SYMLOOP_MAX >= 8, "symloop guard must be nontrivial");
_Static_assert(FL_VFS_PATH_COMPONENTS_MAX >= (unsigned)FL_VFS_SYMLOOP_MAX,
               "path components should cover deep symlink chains");
_Static_assert(FL_VFS_OPEN_FD_MAX >= 16u, "open-fd table cap must be nontrivial");
_Static_assert(FL_VFS_DIRENT_NAME_MAX_CHARS >= 32u, "dirent name cap unexpectedly small");

#endif /* FL_CONTRACT_P5_VFS_H */
