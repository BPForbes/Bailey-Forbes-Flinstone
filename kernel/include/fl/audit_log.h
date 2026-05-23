/* Post-exec audit to FL_AUDIT_REL_DEFAULT when FL_AUDIT is set. GAS: stage line with history_asm_append_record before fwrite. */
#ifndef FL_AUDIT_LOG_H
#define FL_AUDIT_LOG_H

#include "contract.h"
#include "contract_p6_audit_trail.h"
#include "contract_p6_ring_buffer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void fl_audit_set_sink(fl_log_sink_t *sink);
void fl_audit_shell_completed(const char *cmd_line, int host_exit_code);
/** Emit an **authz** line (deny/allow) when **FL_AUDIT** is enabled; **cmd_no** is 0 for foreign exec. */
void fl_audit_authz_event(const char *cmd_line, unsigned cmd_no, int denied);
/** Emit **P2-4** elevation grant/revoke (**grant** non-zero = grant, zero = revoke). */
void fl_audit_elevation_event(const char *principal, const char *reason, int grant);
int fl_audit_show_last_lines(int n);

void fl_ring_log_append_line(const char *line);
unsigned fl_ring_log_drop_count(void);
void fl_ring_log_reset(void);
size_t fl_ring_log_copy_out(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FL_AUDIT_LOG_H */
