/*
 * P0-1/P0-2 contract bundle (driver, net, log sink, authz, fl_result_t).
 * Related: fl/history_record.h, fl/audit_log.h, fl/jail_contract.h;
 * VFS: fl/vfs.h.
 */
#ifndef FL_CONTRACT_H
#define FL_CONTRACT_H

#define FL_CONTRACT_BUNDLE_REV 2

#include "fl/contract_result.h"
#include "fl/driver/driver.h"
#include "fl/driver/net.h"
#include "fl/contract_log.h"
#include "fl/contract_auth.h"

typedef enum {
    FL_CONTRACT_SURFACE_DRIVER_OPS = 0,
    FL_CONTRACT_SURFACE_NETDEV,
    FL_CONTRACT_SURFACE_LOG_SINK,
    FL_CONTRACT_SURFACE_AUTHZ,
    FL_CONTRACT_SURFACE_FS_JAIL
} fl_contract_surface_t;

#endif /* FL_CONTRACT_H */
