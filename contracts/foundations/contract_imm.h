/**
 * Immutable P0 contract **shape** and **numeric caps** (compile-time only).
 *
 * Log sink and audit helpers share one fixed line capacity; stack buffers use
 * **fl_contract_log_line_buf_t** so sizeof/layout cannot drift from the cap.
 */
#ifndef FL_CONTRACT_IMM_H
#define FL_CONTRACT_IMM_H

#include <stddef.h>

/** One bounded sink / audit text line, including terminating NUL space in helpers. */
#define FL_CONTRACT_LOG_SINK_LINE_CAP 768u

_Static_assert(FL_CONTRACT_LOG_SINK_LINE_CAP >= 256u,
               "FL_CONTRACT_LOG_SINK_LINE_CAP unexpectedly small");
_Static_assert(FL_CONTRACT_LOG_SINK_LINE_CAP <= 4096u,
               "FL_CONTRACT_LOG_SINK_LINE_CAP unexpectedly large");

typedef struct fl_contract_log_line_buf {
    char data[FL_CONTRACT_LOG_SINK_LINE_CAP];
} fl_contract_log_line_buf_t;

_Static_assert(sizeof(fl_contract_log_line_buf_t) == FL_CONTRACT_LOG_SINK_LINE_CAP,
               "fl_contract_log_line_buf_t must pack without trailing padding");

#endif /* FL_CONTRACT_IMM_H */
