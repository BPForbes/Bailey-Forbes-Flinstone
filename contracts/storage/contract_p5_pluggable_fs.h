/* P5-2 Pluggable file systems (module contract, normative). Backend ids, read-only vs
 * read-write policy, fsck obligations before attach on bare-metal (B) or kernel (K)
 * builds; hosted (H) bridges may defer native parsers. Probe uses FL_RESULT_NOSYS when
 * declining, like driver probe skip (P0-2). See docs/ROADMAP.md Phase 5. */
#ifndef FL_CONTRACT_P5_PLUGGABLE_FS_H
#define FL_CONTRACT_P5_PLUGGABLE_FS_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P5_2_PLUGGABLE_FS_CONTRACT_DEFINED 1

typedef uint32_t fl_pluggable_fs_driver_id_t;

#define FL_PLUGGABLE_FS_DRIVER_ID_INVALID ((fl_pluggable_fs_driver_id_t)0u)

typedef enum {
    FL_PLUGGABLE_FS_KIND_UNKNOWN = 0,
    /** In-repo FAT32 host facade and similar lab shims. */
    FL_PLUGGABLE_FS_KIND_FAT32_HOST = 1,
    /** Reserved: native ext4 read-only on bare metal (not implied until implemented). */
    FL_PLUGGABLE_FS_KIND_EXT4_RO_NATIVE_DEFERRED = 2,
    /** Reserved: FUSE or other hosted bridge on H. */
    FL_PLUGGABLE_FS_KIND_FUSE_HOST_BRIDGE_DEFERRED = 3
} fl_pluggable_fs_kind_t;

/** NUL-terminated backend name length bound for registration tables. */
#define FL_PLUGGABLE_FS_NAME_MAX_CHARS 63u

#endif /* FL_CONTRACT_P5_PLUGGABLE_FS_H */
