/**
 * @file contract.h
 * OS-platform **contract bundle** (subsystem boundaries **P0-1** and shared
 * fallible result type **P0-2**).
 *
 * Include this header (or the narrower `fl/contract_*.h` pieces) when touching
 * cross-layer boundaries: **driver ops**, **netdev**, **log sink**, **authz**,
 * and fallible kernel-shaped APIs.
 *
 * **Threading / lifetime:** each surface documents its own rules in the header
 * that defines the concrete ops table (`fl_driver_ops_t`, `fl_net_driver`, …).
 *
 * **API lifecycle:** after a subsystem is marked frozen (global std **#7**),
 * exported contracts are append-only unless versioned with `version/entries`.
 *
 * Canonical includes (this bundle):
 *   - `fl/driver/driver.h` — driver lifecycle / registry (**driver ops**)
 *   - `fl/driver/net.h` — L2 netdev driver placeholder (**netdev**)
 *   - `fl/contract_log.h` — log sink ops (**log sink**)
 *   - `fl/contract_auth.h` — authz predicate type (**auth check**)
 *   - `fl/contract_result.h` — `fl_result_t` / shared error codes (**P0-2**)
 *
 * VFS and other storage contracts live under `fl/vfs.h` (include separately to
 * avoid pulling the VFS API into minimal driver-only translation units).
 */
#ifndef FL_CONTRACT_H
#define FL_CONTRACT_H

/** Increment when this bundle’s shape or semantics change (doc + review). */
#define FL_CONTRACT_BUNDLE_REV 1

#include "fl/contract_result.h"
#include "fl/driver/driver.h"
#include "fl/driver/net.h"
#include "fl/contract_log.h"
#include "fl/contract_auth.h"

typedef enum {
    FL_CONTRACT_SURFACE_DRIVER_OPS = 0,
    FL_CONTRACT_SURFACE_NETDEV,
    FL_CONTRACT_SURFACE_LOG_SINK,
    FL_CONTRACT_SURFACE_AUTHZ
} fl_contract_surface_t;

#endif /* FL_CONTRACT_H */
