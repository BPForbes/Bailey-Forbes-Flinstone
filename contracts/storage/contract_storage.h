/*
 * P5 storage / VFS / durability contract bundle (contracts/storage).
 *
 * Include this header (or individual contract_p5_*.h after contract_extend.h) when
 * implementing or reviewing P5-1 through P5-3 work.
 * Build with -Icontracts/storage alongside -Icontracts/drivers, -Icontracts/networking,
 * -Icontracts/identity, -Icontracts/runtime, and -Icontracts/foundations.
 *
 * Layering: extends contract_extend.h (P0 plus P1 vocabulary). It does not import
 * contract_networking.h or contract_drivers.h — translation units that span block I/O,
 * virtio, and the VFS should include those headers explicitly beside this bundle.
 *
 * Bump FL_CONTRACT_P5_STORAGE_REV when shard set or mandatory P5 vocabulary changes.
 */
#ifndef FL_CONTRACT_STORAGE_H
#define FL_CONTRACT_STORAGE_H

#include "contract_extend.h"

#define FL_CONTRACT_P5_STORAGE_REV 5

#include "contract_p5_vfs.h"
#include "contract_p5_pluggable_fs.h"
#include "contract_p5_page_cache.h"
#include "contract_p5_writeback.h"
#include "contract_p5_server_share.h"
#include "contract_p5_file_perms.h"
#include "contract_p5_file_delivery.h"
#include "contract_p5_member_identity.h"

#define FL_CONTRACT_P5_VOCABULARY_LOCK 1

_Static_assert(FL_CONTRACT_P5_VOCABULARY_LOCK == 1,
               "P5 umbrella must include the full shard set when vocabulary lock is on");

#endif /* FL_CONTRACT_STORAGE_H */
