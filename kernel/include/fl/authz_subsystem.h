/**
 * Hosted **P2-3** subsystem authorization: central **fl_authz_check_fn** policy for
 * **FL_AUTHZ_OP_*** gates (FileManager / netdev / mount / raw disk) before dispatch.
 *
 * Shell builtins use **fl/shell_authz.h**; both layers must agree for the same logical op.
 * Tests may install a temporary hook via **fl_authz_subsystem_set_hook**(NULL, NULL).
 */
#ifndef FL_AUTHZ_SUBSYSTEM_H
#define FL_AUTHZ_SUBSYSTEM_H

#include "contract_p2_authz.h"

#ifdef __cplusplus
extern "C" {
#endif

void fl_authz_subsystem_set_hook(fl_authz_check_fn hook_fn, void *ctx);

/** Default policy: allow; guest denies privileged **FL_AUTHZ_OP_*** codes. */
fl_authz_decision_t fl_authz_subsystem_check(unsigned op, void *ctx);

/** Map a **fl_shell_cmd_no_t** to **FL_AUTHZ_OP_*** for subsystem gates (0 = unspecified). */
unsigned fl_authz_subsystem_op_for_shell_cmd(unsigned cmd_no);

/** Guest **FL_PRINCIPAL=guest** deny list for shell builtins (shared with shell gate). */
fl_authz_decision_t fl_authz_guest_shell_builtin(unsigned cmd_no);

#ifdef __cplusplus
}
#endif

#endif /* FL_AUTHZ_SUBSYSTEM_H */
