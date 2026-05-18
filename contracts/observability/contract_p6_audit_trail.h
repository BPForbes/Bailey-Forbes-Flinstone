/* P6-4 Audit trail (module contract, normative). Security-relevant events (auth, mount,
 * raw I/O) are append-only and separate from FL1 command history. Hosted shell uses
 * **FL_AUDIT** gating and **FL_AUDIT_REL_DEFAULT**; tail **show** clamps **N** and scans
 * only the last **FL_CONTRACT_P6_AUDIT_TAIL_SCAN_BYTES_MAX** of huge files. See
 * docs/ROADMAP.md Phase 6. */
#ifndef FL_CONTRACT_P6_AUDIT_TRAIL_H
#define FL_CONTRACT_P6_AUDIT_TRAIL_H

#include "contract_extend.h"

#define FL_CONTRACT_P6_4_AUDIT_TRAIL_CONTRACT_DEFINED 1

#define FL_AUDIT_REL_DEFAULT ".fl_audit.log"
#define FL_AUDIT_ENV "FL_AUDIT"

/** Default line count for `audit show` when **N** is omitted. */
#define FL_CONTRACT_P6_AUDIT_SHOW_N_DEFAULT 32

/** Inclusive upper bound for `audit show N` (cmd_audit.c enforces). */
#define FL_CONTRACT_P6_AUDIT_SHOW_N_MAX 10000u

/** Tail scan window for very large on-disk audit logs (bytes). */
#define FL_CONTRACT_P6_AUDIT_TAIL_SCAN_BYTES_MAX (2u * 1024u * 1024u)

_Static_assert(FL_CONTRACT_P6_AUDIT_SHOW_N_DEFAULT >= 1u,
               "audit show default must be positive");
_Static_assert(FL_CONTRACT_P6_AUDIT_SHOW_N_MAX >= FL_CONTRACT_P6_AUDIT_SHOW_N_DEFAULT,
               "audit show max must cover default");

#endif /* FL_CONTRACT_P6_AUDIT_TRAIL_H */
