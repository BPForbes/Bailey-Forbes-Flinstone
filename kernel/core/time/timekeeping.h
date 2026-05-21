#ifndef FL_TIMEKEEPING_H
#define FL_TIMEKEEPING_H

#include "contract_result.h"
#include <stdint.h>

/** Monotonic nanoseconds since boot (hosted: CLOCK_MONOTONIC via time.h). */
fl_result_t fl_time_monotonic_ns(int64_t *out_ns);

/** Wall-clock seconds since Unix epoch (hosted: CLOCK_REALTIME). */
fl_result_t fl_time_wall_sec(int64_t *out_sec);

#endif /* FL_TIMEKEEPING_H */
