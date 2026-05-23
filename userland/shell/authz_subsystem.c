#include "fl/authz_subsystem.h"
#include "contract_p2_authz.h"
#include "contract_p2_principal_names.h"
#include "fl_shell_cmd.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static fl_authz_check_fn s_subsys_hook;
static void *s_subsys_hook_ctx;
static pthread_mutex_t s_subsys_mu = PTHREAD_MUTEX_INITIALIZER;

void fl_authz_subsystem_set_hook(fl_authz_check_fn hook_fn, void *ctx) {
    pthread_mutex_lock(&s_subsys_mu);
    s_subsys_hook = hook_fn;
    s_subsys_hook_ctx = ctx;
    pthread_mutex_unlock(&s_subsys_mu);
}

static int principal_is_guest(void) {
    const char *p = getenv(FL_PRINCIPAL_ENV_NAME);
    return p && strcmp(p, FL_PRINCIPAL_GUEST_LITERAL) == 0;
}

static fl_authz_decision_t guest_subsystem_policy(unsigned op, void *ctx) {
    (void)ctx;
    switch (op) {
    case FL_AUTHZ_OP_VFS_PRIVILEGED:
    case FL_AUTHZ_OP_NETDEV_REGISTER:
    case FL_AUTHZ_OP_NETDEV_IO:
    case FL_AUTHZ_OP_MOUNT:
    case FL_AUTHZ_OP_DISK_LOW_LEVEL:
    case FL_AUTHZ_OP_IDENTITY_USERADD:
    case FL_AUTHZ_OP_IDENTITY_USERDEL:
    case FL_AUTHZ_OP_IDENTITY_PASSWD:
        return FL_AUTHZ_DENY;
    default:
        return FL_AUTHZ_ALLOW;
    }
}

fl_authz_decision_t fl_authz_guest_shell_builtin(unsigned cmd_no) {
    switch ((fl_shell_cmd_no_t)cmd_no) {
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
    case FL_SCMD_USERADD:
    case FL_SCMD_USERDEL:
    case FL_SCMD_PASSWD:
        return FL_AUTHZ_DENY;
    default:
        return FL_AUTHZ_ALLOW;
    }
}

fl_authz_decision_t fl_authz_subsystem_check(unsigned op, void *ctx) {
    fl_authz_check_fn hook = NULL;

    pthread_mutex_lock(&s_subsys_mu);
    hook = s_subsys_hook;
    pthread_mutex_unlock(&s_subsys_mu);

    if (hook)
        return hook(op, ctx);
    if (!principal_is_guest())
        return FL_AUTHZ_ALLOW;
    return guest_subsystem_policy(op, ctx);
}

unsigned fl_authz_subsystem_op_for_shell_cmd(unsigned cmd_no) {
    switch ((fl_shell_cmd_no_t)cmd_no) {
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
        return (unsigned)FL_AUTHZ_OP_DISK_LOW_LEVEL;
    case FL_SCMD_LOGIN:
        return (unsigned)FL_AUTHZ_OP_IDENTITY_LOGIN;
    case FL_SCMD_USERADD:
        return (unsigned)FL_AUTHZ_OP_IDENTITY_USERADD;
    case FL_SCMD_USERDEL:
        return (unsigned)FL_AUTHZ_OP_IDENTITY_USERDEL;
    case FL_SCMD_PASSWD:
        return (unsigned)FL_AUTHZ_OP_IDENTITY_PASSWD;
    default:
        return (unsigned)FL_AUTHZ_OP_UNSPECIFIED;
    }
}
