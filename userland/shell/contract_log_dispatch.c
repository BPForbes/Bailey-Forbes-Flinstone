#include "fl/contract_log_dispatch.h"
#include "mem_asm.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static _Atomic time_t s_rate_sec;
static _Atomic unsigned s_rate_count;

static int fl_log_rate_allow(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 1;

    time_t sec = ts.tv_sec;
    time_t prev = atomic_load_explicit(&s_rate_sec, memory_order_relaxed);
    if (sec != prev) {
        if (atomic_compare_exchange_strong(&s_rate_sec, &prev, sec)) {
            atomic_store_explicit(&s_rate_count, 0u, memory_order_relaxed);
        }
    }

    unsigned n = atomic_fetch_add_explicit(&s_rate_count, 1u, memory_order_relaxed);
    return n < (unsigned)FL_LOG_RL_MAX_PER_SEC;
}

void fl_log_rate_limit_reset_for_tests(void) {
    atomic_store_explicit(&s_rate_sec, 0, memory_order_relaxed);
    atomic_store_explicit(&s_rate_count, 0u, memory_order_relaxed);
}

fl_result_t fl_log_sink_emit_line(fl_log_sink_t *sink, int level, int facility,
                                  const char *line) {
    char buf[768];

    if (!sink || !sink->ops || !sink->ops->emit || !line)
        return FL_RESULT_INVAL;

    asm_mem_zero(buf, sizeof buf);
    size_t n = strlen(line);
    if (n >= sizeof buf)
        n = sizeof buf - 1u;
    asm_mem_copy(buf, line, n);
    buf[n] = '\0';

    if (!fl_log_rate_allow())
        return FL_RESULT_ERR;

    sink->ops->emit(sink, level, facility, buf);
    return FL_RESULT_OK;
}

fl_result_t fl_log_sink_vprintf(fl_log_sink_t *sink, int level, int facility,
                                const char *fmt, va_list ap) {
    char buf[768];
    va_list ap2;

    if (!sink || !sink->ops || !sink->ops->emit || !fmt)
        return FL_RESULT_INVAL;

    asm_mem_zero(buf, sizeof buf);
    va_copy(ap2, ap);
    int nw = vsnprintf(buf, sizeof buf, fmt, ap2);
    va_end(ap2);
    if (nw < 0)
        return FL_RESULT_ERR;

    if (!fl_log_rate_allow())
        return FL_RESULT_ERR;

    sink->ops->emit(sink, level, facility, buf);
    return FL_RESULT_OK;
}

fl_result_t fl_log_sink_printf(fl_log_sink_t *sink, int level, int facility,
                               const char *fmt, ...) {
    va_list ap;
    fl_result_t rc;

    va_start(ap, fmt);
    rc = fl_log_sink_vprintf(sink, level, facility, fmt, ap);
    va_end(ap);
    return rc;
}
