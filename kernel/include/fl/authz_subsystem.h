#ifndef FL_AUTHZ_SUBSYSTEM_H
#define FL_AUTHZ_SUBSYSTEM_H

#include "contract_p2_authz.h"

#ifdef __cplusplus
extern "C" {
#endif

void fl_authz_subsystem_set_hook(fl_authz_check_fn hook_fn, void *ctx);

fl_authz_decision_t fl_authz_subsystem_check(unsigned op, void *ctx);

void fl_authz_subsystem_set_deny_audit(void (*sink)(unsigned op));

unsigned fl_authz_subsystem_op_for_shell_cmd(unsigned cmd_no);

int fl_authz_identity_switch_denied(const char *line);

fl_authz_decision_t fl_authz_guest_shell_builtin(unsigned cmd_no);

#ifdef __cplusplus
}
#endif

#endif /* FL_AUTHZ_SUBSYSTEM_H */
