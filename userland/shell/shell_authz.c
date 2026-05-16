#include "fl/shell_authz.h"
#include <stdlib.h>
#include <string.h>

static fl_shell_authz_hook_fn s_hook;
static void *s_hook_ctx;

void fl_shell_authz_set_hook(fl_shell_authz_hook_fn fn, void *ctx) {
    s_hook = fn;
    s_hook_ctx = ctx;
}

static int principal_is_guest(void) {
    const char *p = getenv("FL_PRINCIPAL");
    return p && strcmp(p, "guest") == 0;
}

static fl_authz_decision_t guest_builtin_policy(fl_shell_cmd_no_t no) {
    switch (no) {
    case FL_SCMD_FORMAT:
    case FL_SCMD_SETDISK:
    case FL_SCMD_INITDISK:
    case FL_SCMD_CREATEDISK:
    case FL_SCMD_RMTREE:
    case FL_SCMD_DISKPUT:
    case FL_SCMD_DISKGET:
    case FL_SCMD_DISKDEL:
    case FL_SCMD_DISKMKDIR:
    case FL_SCMD_REDIRECT:
    case FL_SCMD_IMPORT:
    case FL_SCMD_WRITE:
    case FL_SCMD_WRITECLUSTER:
    case FL_SCMD_DELCLUSTER:
    case FL_SCMD_UPDATE:
    case FL_SCMD_ADDCLUSTER:
        return FL_AUTHZ_DENY;
    default:
        return FL_AUTHZ_ALLOW;
    }
}

fl_authz_decision_t fl_shell_authz_builtin(fl_shell_cmd_no_t no, int argc, char **argv) {
    if (s_hook)
        return s_hook(no, argc, argv, s_hook_ctx);
    if (!principal_is_guest())
        return FL_AUTHZ_ALLOW;
    return guest_builtin_policy(no);
}

fl_authz_decision_t fl_shell_authz_foreign_exec(void) {
    if (s_hook)
        return s_hook(FL_SCMD_UNKNOWN, 0, NULL, s_hook_ctx);
    if (!principal_is_guest())
        return FL_AUTHZ_ALLOW;
    return FL_AUTHZ_DENY;
}
