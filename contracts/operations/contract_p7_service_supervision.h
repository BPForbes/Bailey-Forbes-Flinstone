/* P7-1 Service supervision (module contract, normative). Hosted start/stop/restart for
 * long-running daemons uses PID files with stale detection and bounded graceful shutdown.
 * Fallible control plane ops use fl_result_t (P0-2). See docs/ROADMAP.md Phase 7. */
#ifndef FL_CONTRACT_P7_SERVICE_SUPERVISION_H
#define FL_CONTRACT_P7_SERVICE_SUPERVISION_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P7_1_SERVICE_SUPERVISION_CONTRACT_DEFINED 1

typedef uint32_t fl_supervised_service_id_t;

#define FL_SUPERVISED_SERVICE_ID_INVALID ((fl_supervised_service_id_t)0u)

/** Relative PID file path segment cap (under runtime state dir on hosted builds). */
#define FL_CONTRACT_P7_PID_FILE_PATH_MAX_CHARS 255u

/** NUL-terminated service name length bound for registration tables. */
#define FL_CONTRACT_P7_SERVICE_NAME_MAX_CHARS 63u

/** Default graceful shutdown wait before SIGKILL-style escalation (seconds). */
#define FL_CONTRACT_P7_GRACEFUL_SHUTDOWN_TIMEOUT_SEC_DEFAULT 30u

/** Soft cap on concurrently supervised services in lab builds. */
#define FL_CONTRACT_P7_SUPERVISED_SERVICE_MAX 32u

/** Supervision control-plane state (hosted; distinct from POSIX process state). */
typedef enum {
    FL_CONTRACT_P7_SERVICE_STATE_STARTING = 0,
    FL_CONTRACT_P7_SERVICE_STATE_RUNNING = 1,
    FL_CONTRACT_P7_SERVICE_STATE_STOPPING = 2,
    FL_CONTRACT_P7_SERVICE_STATE_STOPPED = 3,
    FL_CONTRACT_P7_SERVICE_STATE_FAILED = 4
} fl_contract_p7_service_state_t;

/** Max automatic restart attempts before entering FAILED (restart-storm guard). */
#define FL_CONTRACT_P7_RESTART_MAX_ATTEMPTS_DEFAULT 5u

/** Minimum cooldown between restart attempts (seconds). */
#define FL_CONTRACT_P7_RESTART_COOLDOWN_SEC_DEFAULT 10u

typedef enum {
    FL_CONTRACT_P7_PID_STALE_POLICY_REJECT = 0,
    FL_CONTRACT_P7_PID_STALE_POLICY_REMOVE_AND_CONTINUE = 1
} fl_contract_p7_pid_stale_policy_t;

_Static_assert(FL_CONTRACT_P7_GRACEFUL_SHUTDOWN_TIMEOUT_SEC_DEFAULT >= 1u,
               "graceful shutdown timeout must be positive");
_Static_assert(FL_CONTRACT_P7_RESTART_MAX_ATTEMPTS_DEFAULT >= 1u,
               "restart attempt cap must be positive");
_Static_assert(FL_CONTRACT_P7_RESTART_COOLDOWN_SEC_DEFAULT >= 1u,
               "restart cooldown must be positive");

#endif /* FL_CONTRACT_P7_SERVICE_SUPERVISION_H */
