/**
 * Hosted-shell helpers for **fl_log_sink_t** (**P0-1**, **P6-1**).
 *
 * Formatted lines use **fl_contract_log_line_buf_t** (**fl/contract_imm.h**),
 * cleared with **fl_contract_mem_zero** and copied with **fl_contract_mem_copy**
 * (**fl/contract_asm.h**) before **vsnprintf** / delivery so the path stays on the
 * same **mem_asm** objects as **ARCH=x86_64_gas**,
 * **ARCH=x86_64_nasm**, and **ARCH=arm** (AArch64 GAS).
 *
 * **Rate limit:** at most **FL_LOG_RL_MAX_PER_SEC** sink deliveries per monotonic
 * second (drops return **FL_RESULT_ERR**). A **pthread** mutex serializes the
 * counter against second rollovers. Not IRQ-safe: do not call from a true
 * hardirq; use a deferred path or an IRQ-safe sink implementation instead.
 */
#ifndef FL_CONTRACT_LOG_DISPATCH_H
#define FL_CONTRACT_LOG_DISPATCH_H

#include "fl/contract_imm.h"
#include "fl/contract_log.h"
#include "fl/contract_result.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Syslog-style facility constants (subset). */
#define FL_LOG_FACILITY_USER 1
#define FL_LOG_FACILITY_AUDIT 9

#ifndef FL_LOG_RL_MAX_PER_SEC
#define FL_LOG_RL_MAX_PER_SEC 64
#endif

fl_result_t fl_log_sink_emit_line(fl_log_sink_t *sink, int level, int facility,
                                  const char *line);
fl_result_t fl_log_sink_vprintf(fl_log_sink_t *sink, int level, int facility,
                                const char *fmt, va_list ap);
fl_result_t fl_log_sink_printf(fl_log_sink_t *sink, int level, int facility,
                               const char *fmt, ...);

/** Test hook: clears the per-second emit counter. */
void fl_log_rate_limit_reset_for_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_CONTRACT_LOG_DISPATCH_H */
