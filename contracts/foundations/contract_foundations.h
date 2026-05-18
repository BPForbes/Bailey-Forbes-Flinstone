/*
 * Canonical **P0** contract aggregate — **P0-1** (subsystem boundaries), **P0-2**
 * (**fl_result_t**), and **P0-3**–**P0-8** normative headers (**contract_p0_*.h**)
 * for CI, GIC EOI, x86 IDT/IRQ0, x86 GDT, DTB, and early UART expectations.
 * This is the **only** header Px (*P1–P9*) contract modules should treat as the
 * **vocabulary base**: include it (or **contract_extend.h**) **before** any Px-only
 * declarations, analogous to inheriting one base class in a higher-level language
 * rather than re-stating P0 types or caps in a second header.
 *
 * **Rules for Px authors**
 *   - Do **not** redefine **fl_result_t**, **fl_log_sink_t**, authz enums, line
 *     buffer caps, or **FL_CONTRACT_SURFACE_*** values; add new symbols only in
 *     Px headers (for example under the contracts/extensions/ tree when present).
 *   - Depend on **FL_CONTRACT_P0_FOUNDATIONS_REV** (bumped only when this file’s
 *     include order or mandatory P0 vocabulary changes in a way Px must notice).
 *   - Depend on **FL_CONTRACT_BUNDLE_REV** for the user-visible shipped bundle number
 *     (audit/history metadata); bump **version/entries** when it changes.
 *
 * **Surfaces (P0-1)** — interchange loci covered by this bundle; see **fl/jail_contract.h**
 * for FS jail behaviour wired as **FL_CONTRACT_SURFACE_FS_JAIL**.
 *
 * Related: **fl/history_record.h**, **fl/audit_log.h**, **fl/jail_contract.h**; VFS: **fl/vfs.h**.
 *
 * Build: add **-Icontracts/foundations** (see root **Makefile** / **CMakeLists.txt**).
 */
#ifndef FL_CONTRACT_FOUNDATIONS_H
#define FL_CONTRACT_FOUNDATIONS_H

/** Bump when P0 aggregate layout or required Px prelude changes (Px may _Static_assert). */
#define FL_CONTRACT_P0_FOUNDATIONS_REV 4

/*
 * Include order: result + auth (no deps) → imm + asm (caps, mem) → log + dispatch
 * → driver/net (use **fl_result_t** from result).
 */
#include "contract_result.h"
#include "contract_auth.h"
#include "contract_imm.h"
#include "contract_asm.h"
#include "contract_log_dispatch.h"
#include "fl/driver/driver.h"
#include "fl/driver/net.h"

/* P0-3 … P0-8 — normative contract prose + markers (implementation elsewhere). */
#include "contract_p0_ci.h"
#include "contract_p0_arm_gic.h"
#include "contract_p0_x86_idt.h"
#include "contract_p0_x86_gdt.h"
#include "contract_p0_fdt.h"
#include "contract_p0_uart.h"

/** Shipped subsystem contract bundle revision (audit / CLI / packed metadata). */
#define FL_CONTRACT_BUNDLE_REV 6

typedef enum {
    FL_CONTRACT_SURFACE_DRIVER_OPS = 0,
    FL_CONTRACT_SURFACE_NETDEV,
    FL_CONTRACT_SURFACE_LOG_SINK,
    FL_CONTRACT_SURFACE_AUTHZ,
    FL_CONTRACT_SURFACE_FS_JAIL,
    /** One past the last real surface; use for bounds checks / table sizes. */
    FL_CONTRACT_SURFACE_COUNT
} fl_contract_surface_t;

_Static_assert((int)FL_CONTRACT_SURFACE_COUNT == 5,
               "fl_contract_surface_t ABI: expected five surfaces before COUNT");

/*
 * P0-1 per-surface interchange metadata (lifetime + concurrency + error channel).
 * Informational for P1+; bump **FL_CONTRACT_P0_FOUNDATIONS_REV** when these change.
 */
#define FL_CONTRACT_SURFACE_DRIVER_OPS_LIFETIME_STATIC 0
#define FL_CONTRACT_SURFACE_NETDEV_LIFETIME_REGISTRATION 0
#define FL_CONTRACT_SURFACE_LOG_SINK_LIFETIME_CALLER_STACK 0
#define FL_CONTRACT_SURFACE_AUTHZ_LIFETIME_EVENT_TRANSIENT 0
#define FL_CONTRACT_SURFACE_FS_JAIL_LIFETIME_POLICY_STATIC 0

#define FL_CONTRACT_P0_SURFACE_CONCURRENCY_SINGLE_CALLER 0
#define FL_CONTRACT_P0_SURFACE_CONCURRENCY_LOCK_GUARDED 1
#define FL_CONTRACT_P0_SURFACE_CONCURRENCY_LOCK_FREE 2

#define FL_CONTRACT_SURFACE_DRIVER_OPS_CONCURRENCY \
    FL_CONTRACT_P0_SURFACE_CONCURRENCY_LOCK_GUARDED
#define FL_CONTRACT_SURFACE_NETDEV_CONCURRENCY \
    FL_CONTRACT_P0_SURFACE_CONCURRENCY_LOCK_GUARDED
#define FL_CONTRACT_SURFACE_LOG_SINK_CONCURRENCY \
    FL_CONTRACT_P0_SURFACE_CONCURRENCY_LOCK_GUARDED
#define FL_CONTRACT_SURFACE_AUTHZ_CONCURRENCY \
    FL_CONTRACT_P0_SURFACE_CONCURRENCY_SINGLE_CALLER
#define FL_CONTRACT_SURFACE_FS_JAIL_CONCURRENCY \
    FL_CONTRACT_P0_SURFACE_CONCURRENCY_LOCK_GUARDED

/** Primary status path: fallible **fl_result_t** (see **contract_result.h**). */
#define FL_CONTRACT_SURFACE_ERROR_FL_RESULT 0

#define FL_CONTRACT_SURFACE_DRIVER_OPS_ERROR FL_CONTRACT_SURFACE_ERROR_FL_RESULT
#define FL_CONTRACT_SURFACE_NETDEV_ERROR FL_CONTRACT_SURFACE_ERROR_FL_RESULT
#define FL_CONTRACT_SURFACE_LOG_SINK_ERROR FL_CONTRACT_SURFACE_ERROR_FL_RESULT
#define FL_CONTRACT_SURFACE_AUTHZ_ERROR FL_CONTRACT_SURFACE_ERROR_FL_RESULT
#define FL_CONTRACT_SURFACE_FS_JAIL_ERROR FL_CONTRACT_SURFACE_ERROR_FL_RESULT

/** Set after full P0 vocabulary is visible; Px optional guards may `#if FL_CONTRACT_P0_LOCK`. */
#define FL_CONTRACT_P0_VOCABULARY_LOCK 1

#include "contract_compile_ext.h"

#endif /* FL_CONTRACT_FOUNDATIONS_H */
