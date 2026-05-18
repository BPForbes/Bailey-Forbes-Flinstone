/* P6-3 Persistent log (hosted) (module contract, normative). Append-only file under a
 * configurable directory; rotation by size on **H**; fsync policy and secret-redaction hook
 * count are frozen for interchange reviews. Wire export may follow **RFC 5424** later. See
 * docs/ROADMAP.md Phase 6. */
#ifndef FL_CONTRACT_P6_PERSISTENT_LOG_H
#define FL_CONTRACT_P6_PERSISTENT_LOG_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P6_3_PERSISTENT_LOG_CONTRACT_DEFINED 1

typedef enum {
    FL_CONTRACT_P6_FSYNC_POLICY_NONE = 0,
    FL_CONTRACT_P6_FSYNC_POLICY_EACH_LINE = 1,
    FL_CONTRACT_P6_FSYNC_POLICY_ON_ROTATE = 2
} fl_contract_p6_fsync_policy_t;

/** Default rotation threshold when hosted persistent logging is enabled (bytes). */
#define FL_CONTRACT_P6_PERSISTENT_LOG_ROTATE_BYTES_DEFAULT (16u * 1024u * 1024u)

/** Max registered redaction hook slots (implementation may use fewer). */
#define FL_CONTRACT_P6_SECRET_REDACTION_HOOK_MAX 8u

/** Relative path segment cap for hosted log directory + file name reviews. */
#define FL_CONTRACT_P6_PERSISTENT_LOG_PATH_MAX_CHARS 255u

#endif /* FL_CONTRACT_P6_PERSISTENT_LOG_H */
