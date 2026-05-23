/**
 * **P0 — VFS jail surface** (module contract, normative).
 *
 * **Interchange blueprint** (**FL_CONTRACT_P0_FS_JAIL_INTERCHANGE_REV**): hosted VM
 * sandbox for the local file provider. Pairs with **FL_CONTRACT_SURFACE_FS_JAIL**
 * in **contract_foundations.h**. Implementation: **kernel/core/vfs/fs_jail.h**.
 *
 * **Normative host subdirectory** (under VM launch root / cwd):
 *   **FS_JAIL_HOST_SUBDIR** — `"vm_hostfs"` (must match **fs_jail.h**).
 *
 * **Return semantics** (**fs_jail_check_path**, **fs_jail_check_access**):
 *   - **0** — path is inside the jail and elevation policy permits access.
 *   - **-1** — denied (outside jail and/or **requires_elevation** without active
 *     elevation per **P2-4** / **fl_session_has_elevation()**).
 *   - Callers may observe **errno = EPERM** on elevation miss (**fs_jail_check_access**).
 *
 * **TOCTOU** (**fs_jail_openat**):
 *   - Open with **O_NOFOLLOW** (and caller flags); post-open canonical check that the
 *     opened object remains inside the jail root.
 *
 * **P2 composition:**
 *   - **fs_jail_check_access** consults **fl_path_property_resolve** (**P2-1**) and
 *     elevation state before allowing provider I/O.
 *   - Privileged VFS ops in **fs_facade** use **FL_AUTHZ_OP_VFS_PRIVILEGED** (**P2-3**).
 *
 * Bump **FL_CONTRACT_P0_FS_JAIL_INTERCHANGE_REV** with a **version/entries** note when
 * return semantics or **FS_JAIL_HOST_SUBDIR** change.
 */
#ifndef FL_CONTRACT_P0_FS_JAIL_H
#define FL_CONTRACT_P0_FS_JAIL_H

#include "contract_extend.h"

#define FL_CONTRACT_P0_FS_JAIL_INTERCHANGE_REV 1

#define FL_CONTRACT_P0_FS_JAIL_SURFACE_CHECK_PATH 0
#define FL_CONTRACT_P0_FS_JAIL_SURFACE_CHECK_ACCESS 1
#define FL_CONTRACT_P0_FS_JAIL_SURFACE_OPENAT 2
#define FL_CONTRACT_P0_FS_JAIL_SURFACE_COUNT 3

/** Normative jail host subdir name (repository-relative under VM root). */
#define FL_CONTRACT_P0_FS_JAIL_HOST_SUBDIR "vm_hostfs"

#define FL_CONTRACT_P0_FS_JAIL_RETURN_ALLOWED 0
#define FL_CONTRACT_P0_FS_JAIL_RETURN_DENIED (-1)

#define FL_CONTRACT_P0_FS_JAIL_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P0_FS_JAIL_H */
