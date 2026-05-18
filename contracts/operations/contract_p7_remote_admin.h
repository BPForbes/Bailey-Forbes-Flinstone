/* P7-3 Remote admin path (module contract, normative). Lab reverse-shell helpers are
 * compile-gated (**CONFIG_LAB_REVERSE_SHELL**, default off). Enablement must emit a
 * **P6-4** audit record when audit sinks are active. SSH-scale remote admin is out of
 * scope for this shard. See docs/ROADMAP.md Phase 7. */
#ifndef FL_CONTRACT_P7_REMOTE_ADMIN_H
#define FL_CONTRACT_P7_REMOTE_ADMIN_H

#include "contract_extend.h"
#include "contract_p6_audit_trail.h"

#define FL_CONTRACT_P7_3_REMOTE_ADMIN_CONTRACT_DEFINED 1

/** Compile-time symbol name for lab reverse-shell (must be 0 in default presets). */
#define FL_CONTRACT_P7_REVERSE_SHELL_CONFIG_SYMBOL CONFIG_LAB_REVERSE_SHELL

#ifndef CONFIG_LAB_REVERSE_SHELL
#define CONFIG_LAB_REVERSE_SHELL 0
#endif

/** Documented default for all release-style and CI presets. */
#define FL_CONTRACT_P7_REVERSE_SHELL_DEFAULT_OFF 0

_Static_assert(CONFIG_LAB_REVERSE_SHELL == 0 || CONFIG_LAB_REVERSE_SHELL == 1,
               "CONFIG_LAB_REVERSE_SHELL must be 0 or 1");

/** When non-zero, implementations must log enablement via **P6-4** audit before use. */
#define FL_CONTRACT_P7_REVERSE_SHELL_REQUIRES_AUDIT_ON_ENABLE 1

/** Audit category for enablement lines (**fl_contract_p6_audit_event_kind_t**). */
#define FL_CONTRACT_P7_REVERSE_SHELL_AUDIT_EVT FL_CONTRACT_P6_AUDIT_EVT_RAW_IO

#endif /* FL_CONTRACT_P7_REMOTE_ADMIN_H */
