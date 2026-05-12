/**
 * @file jail_contract.h
 *
 * **Host sandbox (fs jail)** discipline for userland and tooling:
 *
 * - After `fs_jail_init()` in VM mode, the process cwd is the resolved jail root
 *   under `vm_hostfs/`. Persisted artifacts such as **`.fl_audit.log`**
 *   (**fl/audit_log.h**, `FL_AUDIT_REL_DEFAULT`) and relative host paths must stay
 *   under that tree unless the jail is inactive.
 * - Before using untrusted-derived host paths, call `fs_jail_check_path()` when
 *   `fs_jail_is_active()`; prefer `fs_jail_openat()` for opens (see `fs_jail.h`).
 *
 * The contract surface **`FL_CONTRACT_SURFACE_FS_JAIL`** marks APIs and modules
 * that participate in this boundary (see **`fl/contract.h`**).
 */
#ifndef FL_JAIL_CONTRACT_H
#define FL_JAIL_CONTRACT_H

#endif /* FL_JAIL_CONTRACT_H */
