#include "contract_asm.h"
#include "contract_log_dispatch.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t s_rate_mu = PTHREAD_MUTEX_INITIALIZER;
static time_t s_rate_sec;
static unsigned s_rate_count;

static int fl_log_rate_allow(void) {
    struct timespec ts;
    pthread_mutex_lock(&s_rate_mu);
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        pthread_mutex_unlock(&s_rate_mu);
        return 1;
    }
    time_t sec = ts.tv_sec;
    if (sec != s_rate_sec) {
        s_rate_sec = sec;
        s_rate_count = 0u;
    }
    unsigned n = s_rate_count++;
    pthread_mutex_unlock(&s_rate_mu);
    return n < (unsigned)FL_LOG_RL_MAX_PER_SEC;
}

void fl_log_rate_limit_reset_for_tests(void) {
    pthread_mutex_lock(&s_rate_mu);
    s_rate_sec = 0;
    s_rate_count = 0u;
    pthread_mutex_unlock(&s_rate_mu);
}

fl_result_t fl_log_sink_emit_line(fl_log_sink_t *sink, int level, int facility,
                                  const char *line) {
    fl_contract_log_line_buf_t buf;

    if (!sink || !sink->ops || !sink->ops->emit || !line)
        return FL_RESULT_INVAL;

    fl_contract_mem_zero(buf.data, sizeof buf.data);
    size_t n = strlen(line);
    if (n >= sizeof buf.data)
        n = sizeof buf.data - 1u;
    fl_contract_mem_copy(buf.data, line, n);
    buf.data[n] = '\0';

    if (!fl_log_rate_allow())
        return FL_RESULT_ERR;

    sink->ops->emit(sink, level, facility, buf.data);
    return FL_RESULT_OK;
}

fl_result_t fl_log_sink_vprintf(fl_log_sink_t *sink, int level, int facility,
                                const char *fmt, va_list ap) {
    fl_contract_log_line_buf_t buf;
    va_list ap2;

    if (!sink || !sink->ops || !sink->ops->emit || !fmt)
        return FL_RESULT_INVAL;

    fl_contract_mem_zero(buf.data, sizeof buf.data);
    va_copy(ap2, ap);
    int nw = vsnprintf(buf.data, sizeof buf.data, fmt, ap2);
    va_end(ap2);
    if (nw < 0)
        return FL_RESULT_ERR;

    if (!fl_log_rate_allow())
        return FL_RESULT_ERR;

    sink->ops->emit(sink, level, facility, buf.data);
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
