/**
 * Tiny in-memory ring buffer for recent audit-sized lines (**P6-2** sketch).
 *
 * Thread-safe with a coarse mutex; intended for lab / `audit ring` inspection.
 */
#ifndef FL_RING_LOG_H
#define FL_RING_LOG_H

#include <stddef.h>

/** Byte capacity of the ring (matches internal buffer). */
#define FL_RING_LOG_CAPACITY 8192u

#ifdef __cplusplus
extern "C" {
#endif

void fl_ring_log_append_line(const char *line);
unsigned fl_ring_log_drop_count(void);
void fl_ring_log_reset(void);
/** Copy NUL-terminated ring text into **buf** (truncated with final '\0'). */
size_t fl_ring_log_copy_out(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FL_RING_LOG_H */
