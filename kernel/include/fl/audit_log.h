/**
 * @file audit_log.h
 * **Audit trail** for shell execution: separate from persisted command history
 * (**fl/history_record.h**). One line is appended per completed `execute_command_str`
 * when **`FL_AUDIT`** is set (non-empty, not `0`).
 *
 * **Jail discipline:** lines are written to **`FL_AUDIT_REL_DEFAULT`** relative to
 * the process cwd. In VM sandbox mode, `fs_jail_init()` has already `chdir`'d into
 * the resolved jail root, so the audit file stays inside the host sandbox without
 * exposing repository paths.
 *
 * Optional **`fl_log_sink_t`** mirror: set with `fl_audit_set_sink()`.
 */
#ifndef FL_AUDIT_LOG_H
#define FL_AUDIT_LOG_H

#include "fl/contract.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_AUDIT_REL_DEFAULT ".fl_audit.log"
#define FL_AUDIT_ENV "FL_AUDIT"

void fl_audit_set_sink(fl_log_sink_t *sink);

/**
 * Record one completed shell line with host-style exit status (0 success, else
 * failure). Maps to `fl_result_t` for the sink mirror (`0` -> OK, else ERR).
 */
void fl_audit_shell_completed(const char *cmd_line, int host_exit_code);

/** Print the last `n` lines of `FL_AUDIT_REL_DEFAULT` (best-effort). Returns 0 on success. */
int fl_audit_show_last_lines(int n);

#ifdef __cplusplus
}
#endif

#endif /* FL_AUDIT_LOG_H */
