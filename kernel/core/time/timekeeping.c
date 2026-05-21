/* TODO(contract-mount/GH-123): timekeeping.h anchors contract_p1_timekeeping.h. */
/* TODO(P1/Codex): P1-7 arch time on B — AArch64 Generic Timer or P0-5 tick path;
 * hosted POSIX clock_gettime is reference-only on H. */
#include "timekeeping.h"
#include <time.h>

fl_result_t fl_time_monotonic_ns(int64_t *out_ns) {
    struct timespec ts;
    if (!out_ns)
        return FL_RESULT_INVAL;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return FL_RESULT_ERR;
#else
    return FL_RESULT_ERR;
#endif
    *out_ns = (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    return FL_RESULT_OK;
}

fl_result_t fl_time_wall_sec(int64_t *out_sec) {
    struct timespec ts;
    if (!out_sec)
        return FL_RESULT_INVAL;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return FL_RESULT_ERR;
    *out_sec = (int64_t)ts.tv_sec;
    return FL_RESULT_OK;
}
