/*
 * FL1 + RS + JSON + RS + cmd + LF persisted history; RS or LF in cmd
 * forces legacy plain line.
 * GAS: cmd tail via history_asm_append_record.
 */
#ifndef FL_HISTORY_RECORD_H
#define FL_HISTORY_RECORD_H

#include "contract.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_HISTORY_RECORD_TAG "FL1"
#define FL_HISTORY_RECORD_SEP ((char)0x1e) /* ASCII RS */

/* Returns number of bytes written on success, (size_t)-1 on error. */
size_t fl_history_record_pack(char *out, size_t out_cap, uint8_t bundle_rev,
                              fl_contract_surface_t surface, fl_result_t last_rc,
                              const char *cmd);

/*
 * Unpacks stored history line into cmd_out (NUL-terminated, limited by
 * cmd_out_cap) and optional metadata. Returns 1 on success (FL1 format
 * parsed), 0 on legacy fallback (plain text copied to cmd_out).
 * Returns -1 when an FL1-tagged line is malformed (missing separator,
 * invalid JSON metadata, or out-of-range fields): cmd_out is cleared to
 * an empty string when cmd_out_cap > 0; optional metadata outputs are
 * left unchanged if non-NULL.
 * On success: all non-NULL output parameters are written; on fallback:
 * cmd_out is written, metadata outputs left unchanged if non-NULL.
 */
int fl_history_record_unpack_cmd(const char *stored_line, char *cmd_out,
                                 size_t cmd_out_cap, uint8_t *bundle_rev_out,
                                 fl_contract_surface_t *surface_out,
                                 fl_result_t *rc_out);

#ifdef __cplusplus
}
#endif

#endif /* FL_HISTORY_RECORD_H */
