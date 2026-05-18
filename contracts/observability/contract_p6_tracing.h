/* P6-5 Tracing hooks (module contract, normative). Static tracepoints for scheduler, net,
 * and block hot paths. When **FL_CONTRACT_P6_TRACE_ENABLE** is 0, **FL_CONTRACT_P6_TRACE**
 * expands to a no-op (zero cost when disabled). DTrace-style naming is optional for
 * implementers. See docs/ROADMAP.md Phase 6. */
#ifndef FL_CONTRACT_P6_TRACING_H
#define FL_CONTRACT_P6_TRACING_H

#include "contract_extend.h"

#define FL_CONTRACT_P6_5_TRACING_CONTRACT_DEFINED 1

typedef enum {
    FL_CONTRACT_P6_TRACE_SCHEDULER = 0,
    FL_CONTRACT_P6_TRACE_NET = 1,
    FL_CONTRACT_P6_TRACE_BLOCK = 2,
    FL_CONTRACT_P6_TRACE_VFS = 3,
    FL_CONTRACT_P6_TRACE_COUNT
} fl_contract_p6_tracepoint_t;

#ifndef FL_CONTRACT_P6_TRACE_ENABLE
#define FL_CONTRACT_P6_TRACE_ENABLE 0
#endif

#if FL_CONTRACT_P6_TRACE_ENABLE
#define FL_CONTRACT_P6_TRACE(tp, ...) fl_contract_p6_trace_emit((tp), __VA_ARGS__)
void fl_contract_p6_trace_emit(fl_contract_p6_tracepoint_t tp, const char *fmt, ...);
#else
#define FL_CONTRACT_P6_TRACE(tp, ...) ((void)(tp))
#endif

/** Trace emitters should attach **fl_contract_p6_correlation_id_t** when **P6-1** logging is active. */

_Static_assert((int)FL_CONTRACT_P6_TRACE_COUNT == 4,
               "P6-5 tracepoint table size");

#endif /* FL_CONTRACT_P6_TRACING_H */
