/* Post-exec audit to FL_AUDIT_REL_DEFAULT when FL_AUDIT is set. GAS: stage line with history_asm_append_record before fwrite. */
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
void fl_audit_shell_completed(const char *cmd_line, int host_exit_code);
/** Emit an **authz** line (deny/allow) when **FL_AUDIT** is enabled; **cmd_no** is 0 for foreign exec. */
void fl_audit_authz_event(const char *cmd_line, unsigned cmd_no, int denied);
int fl_audit_show_last_lines(int n);

#ifdef __cplusplus
}
#endif

#endif /* FL_AUDIT_LOG_H */
