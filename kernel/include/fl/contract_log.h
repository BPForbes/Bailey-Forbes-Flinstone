/**
 * Structured log **sink** contract (**P0-1**, **P6-1**).
 *
 * Severity names follow **syslog(3)** conventions for familiarity. Implementations
 * must document whether `emit` is IRQ-safe and whether allocations are allowed.
 */
#ifndef FL_CONTRACT_LOG_H
#define FL_CONTRACT_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FL_LOG_EMERG = 0,
    FL_LOG_ALERT,
    FL_LOG_CRIT,
    FL_LOG_ERR,
    FL_LOG_WARNING,
    FL_LOG_NOTICE,
    FL_LOG_INFO,
    FL_LOG_DEBUG
} fl_log_level_t;

struct fl_log_sink;

typedef void (*fl_log_sink_emit_fn)(struct fl_log_sink *sink, int level,
                                    int facility, const char *msg);

typedef struct fl_log_sink_ops {
    fl_log_sink_emit_fn emit;
} fl_log_sink_ops_t;

typedef struct fl_log_sink {
    const fl_log_sink_ops_t *ops;
    void *impl;
} fl_log_sink_t;

/* Hosted printf-style delivery + rate limit: fl/contract_log_dispatch.h */

#ifdef __cplusplus
}
#endif

#endif /* FL_CONTRACT_LOG_H */
