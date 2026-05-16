/**
 * Hosted-shell authorization (**P2-3**): gate builtins and foreign **execvp** before dispatch.
 *
 * Default policy: allow. When **FL_PRINCIPAL=guest**, deny a fixed set of disk / format /
 * destructive builtins and deny launching arbitrary host binaries.
 *
 * Tests may install a temporary hook via **fl_shell_authz_set_hook**(NULL) to restore default.
 */
#ifndef FL_SHELL_AUTHZ_H
#define FL_SHELL_AUTHZ_H

#include "fl/contract_auth.h"
#include "fl_shell_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef fl_authz_decision_t (*fl_shell_authz_hook_fn)(fl_shell_cmd_no_t no, int argc,
                                                      char **argv, void *ctx);

void fl_shell_authz_set_hook(fl_shell_authz_hook_fn hook_fn, void *ctx);

fl_authz_decision_t fl_shell_authz_builtin(fl_shell_cmd_no_t no, int argc, char **argv);

fl_authz_decision_t fl_shell_authz_foreign_exec(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* FL_SHELL_AUTHZ_H */
