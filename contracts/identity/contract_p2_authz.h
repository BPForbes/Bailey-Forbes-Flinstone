/**
 * **P2-3 — Authorization middleware** (module contract, normative).
 *
 * Inherits **P0** plus **P1** through **contract_runtime.h**. Uses **fl_result_t**,
 * **FL_RESULT_ACCES**, and **fl_authz_decision_t** / **fl_authz_check_fn** from the
 * foundations bundle (**contract_auth.h**).
 *
 * **Two layers**
 *   - **Subsystem gate**: **fl_authz_check_fn** with **fl_authz_operation_t** codes
 *     below — central **can_*** before FileManager, netdev, mount, and raw disk.
 *   - **Shell gate**: **fl_shell_authz_*** in **fl/shell_authz.h** (command numbers
 *     + **FL_PRINCIPAL_GUEST_LITERAL** policy). Both must deny/allow consistently
 *     for the same logical operation where both exist.
 *
 * Obligations:
 *   - Deny-by-default for **guest** on privileged **FL_AUTHZ_OP_*** checks once wired.
 *   - Log denies and allows per **P6** expectations when audit sinks are active.
 *
 * See **docs/ROADMAP.md** Phase 2 and **P2 → P3** gate notes.
 */
#ifndef FL_CONTRACT_P2_AUTHZ_H
#define FL_CONTRACT_P2_AUTHZ_H

#include "contract_runtime.h"
#include "contract_auth.h"

#define FL_CONTRACT_P2_3_AUTHZ_CONTRACT_DEFINED 1

/**
 * Stable **unsigned** op codes for **fl_authz_check_fn(op, ctx)** on privileged
 * surfaces. **0** is reserved “unspecified”. Shell builtins use **fl_shell_cmd_no_t**
 * through **fl_shell_authz_*** instead of these codes unless a bridge maps them.
 */
typedef enum {
    FL_AUTHZ_OP_UNSPECIFIED = 0,
    FL_AUTHZ_OP_VFS_PRIVILEGED = 1,
    FL_AUTHZ_OP_NETDEV_REGISTER = 2,
    FL_AUTHZ_OP_NETDEV_IO = 3,
    FL_AUTHZ_OP_MOUNT = 4,
    FL_AUTHZ_OP_DISK_LOW_LEVEL = 5,
    /** Foreign **execvp** / host program launch from the shell (hosted). */
    FL_AUTHZ_OP_SHELL_FOREIGN_EXEC = 100
} fl_authz_operation_t;

_Static_assert((unsigned)FL_AUTHZ_OP_SHELL_FOREIGN_EXEC > (unsigned)FL_AUTHZ_OP_DISK_LOW_LEVEL,
               "shell foreign-exec band separated from low-numbered subsystem ops");

#endif /* FL_CONTRACT_P2_AUTHZ_H */
