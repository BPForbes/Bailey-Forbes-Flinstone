/* FL1 + RS + JSON + RS + cmd + LF persisted history; RS or LF in cmd forces legacy plain line. GAS: cmd tail via history_asm_append_record. */
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

int fl_history_record_unpack_cmd(const char *stored_line, char *cmd_out,
                                 size_t cmd_out_cap, uint8_t *bundle_rev_out,
                                 fl_contract_surface_t *surface_out,
                                 fl_result_t *rc_out);

#ifdef __cplusplus
}
#endif

#endif /* FL_HISTORY_RECORD_H */
