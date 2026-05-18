/* P6-2 Ring buffer sink (module contract, normative). In-memory dmesg-style retention with
 * drop counters; overflow drops oldest line boundaries first. **FL_RING_LOG_CAPACITY** is
 * the implementation alias for **FL_CONTRACT_P6_RING_CAPACITY_BYTES**. See
 * docs/ROADMAP.md Phase 6. */
#ifndef FL_CONTRACT_P6_RING_BUFFER_H
#define FL_CONTRACT_P6_RING_BUFFER_H

#include "contract_extend.h"

#include <stddef.h>

#define FL_CONTRACT_P6_2_RING_BUFFER_CONTRACT_DEFINED 1

/** Max payload bytes (line text including embedded newlines, not counting storage NUL). */
#define FL_CONTRACT_P6_RING_CAPACITY_BYTES 8192u

/** Optional lossless cap (0 = disabled); lab builds may set before including this header. */
#ifndef FL_CONTRACT_P6_RING_LOSSLESS_CAP_BYTES
#define FL_CONTRACT_P6_RING_LOSSLESS_CAP_BYTES 0u
#endif

#ifndef FL_RING_LOG_CAPACITY
#define FL_RING_LOG_CAPACITY FL_CONTRACT_P6_RING_CAPACITY_BYTES
#endif

_Static_assert(FL_CONTRACT_P6_RING_CAPACITY_BYTES >= 512u,
               "ring capacity must be nontrivial");
_Static_assert(FL_RING_LOG_CAPACITY == FL_CONTRACT_P6_RING_CAPACITY_BYTES,
               "FL_RING_LOG_CAPACITY must track FL_CONTRACT_P6_RING_CAPACITY_BYTES");

#endif /* FL_CONTRACT_P6_RING_BUFFER_H */
