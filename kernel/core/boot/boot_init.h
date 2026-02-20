/**
 * Boot init - phased startup before system/shell/VM.
 * Each phase logs [ OK ] or [ FAIL ]; boot_run returns 0 only if all pass.
 */
#ifndef BOOT_INIT_H
#define BOOT_INIT_H

/* Run init phases. disk_file may be NULL (uses current_disk_file).
 * for_vm: 1 = run vm_boot as final phase; 0 = run scheduler init for shell.
 * Returns 0 if all phases pass; -1 if any fail. */
int boot_run(const char *disk_file, int for_vm);

#endif /* BOOT_INIT_H */
