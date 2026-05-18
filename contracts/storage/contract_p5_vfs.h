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

_Static_assert(FL_VFS_SYMLOOP_MAX >= 8, "symloop guard must be nontrivial");
_Static_assert(FL_VFS_PATH_COMPONENTS_MAX >= (unsigned)FL_VFS_SYMLOOP_MAX,
               "path components should cover deep symlink chains");

#endif /* FL_CONTRACT_P5_VFS_H */
