/* P6-1 Structured log API (module contract, normative). Sink types and hosted printf-style
 * delivery live in **contract_log.h** / **contract_log_dispatch.h** (P0-1); this shard
 * records Phase 6 policy: syslog-shaped levels, rate limit, and hardirq context rules
 * (no allocation on the true hardirq path — defer or use an IRQ-safe sink). See
 * docs/ROADMAP.md Phase 6. */
#ifndef FL_CONTRACT_P6_STRUCTURED_LOG_H
#define FL_CONTRACT_P6_STRUCTURED_LOG_H

#include "contract_extend.h"
#include "contract_log.h"

#define FL_CONTRACT_P6_1_STRUCTURED_LOG_CONTRACT_DEFINED 1

/** Default hosted rate limit (per monotonic second); override before including dispatch. */
#ifndef FL_CONTRACT_P6_LOG_RATE_MAX_PER_SEC
#define FL_CONTRACT_P6_LOG_RATE_MAX_PER_SEC 64
#endif

/** Where the emit path may run (informational for reviewers and static analysis). */
typedef enum {
    FL_CONTRACT_P6_LOG_CTX_THREAD = 0,
    /** Hardirq must not call **fl_log_sink_printf** / **fl_log_sink_emit_line** directly. */
    FL_CONTRACT_P6_LOG_CTX_HARDIRQ_DEFER = 1
} fl_contract_p6_log_context_t;

_Static_assert((int)FL_LOG_DEBUG > (int)FL_LOG_EMERG,
               "fl_log_level_t must remain syslog-ordered");

#endif /* FL_CONTRACT_P6_STRUCTURED_LOG_H */
