#ifndef VM_SNAPSHOT_H
#define VM_SNAPSHOT_H

#include "vm_host.h"

/* Snapshot/checkpoint: save and restore VM state (RAM, CPU, kbd, ticks).
 * Uses ASM for all memory copy. PQ can schedule checkpoint tasks. */
int vm_snapshot_save(vm_host_t *host);
int vm_snapshot_restore(vm_host_t *host);
int vm_snapshot_has_checkpoint(void);
void vm_snapshot_shutdown(void);

#endif /* VM_SNAPSHOT_H */
