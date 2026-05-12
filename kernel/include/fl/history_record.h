/**
 * @file history_record.h
 * Persisted **shell command history** line format: optional contract payload
 * prefix (`FL1` + record separator + JSON + separator + command) so the same
 * on-disk layout can be shared by hosted userland and future kernel-shaped
 * replay paths.
 *
 * **Wire format (v1):** `FL1` + ASCII RS (`\\x1e`) + compact JSON +
 * RS + command bytes + LF. JSON keys: `br` (bundle rev), `sf` (surface),
 * `rc` (`fl_result_t` snapshot). Lines without the `FL1` prefix are legacy
 * plain commands.
 *
 * Commands containing RS or LF are stored without a payload (legacy line).
 *
 * On GAS host builds (when DISK_HOST_USE_LIBC_PREADV is not defined), the
 * trailing command bytes plus LF are appended with history_asm_append_record
 * (see shell_history_asm.h and shell_history_host_asm.s under arch).
 *
 * Audit trail (post-exit, optional FL_AUDIT) is separate; see fl/audit_log.h.
 */
#ifndef FL_HISTORY_RECORD_H
#define FL_HISTORY_RECORD_H

#include "fl/contract.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_HISTORY_RECORD_TAG "FL1"
#define FL_HISTORY_RECORD_SEP ((char)0x1e) /* ASCII RS */

size_t fl_history_record_pack(char *out, size_t out_cap, uint8_t bundle_rev,
                              fl_contract_surface_t surface, fl_result_t last_rc,
                              const char *cmd);

/**
 * Copy the executable command portion of one stored history line into `cmd_out`.
 * Returns 1 if a v1 payload was decoded, 0 for legacy plain lines.
 * Output fields are written only when the corresponding pointer is non-NULL.
 */
int fl_history_record_unpack_cmd(const char *stored_line, char *cmd_out,
                                 size_t cmd_out_cap, uint8_t *bundle_rev_out,
                                 fl_contract_surface_t *surface_out,
                                 fl_result_t *rc_out);

#ifdef __cplusplus
}
#endif

#endif /* FL_HISTORY_RECORD_H */
