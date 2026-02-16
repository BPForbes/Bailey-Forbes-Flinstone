#ifndef VM_H
#define VM_H

#include <stdint.h>

#ifdef VM_ENABLE

int vm_boot(void);
void vm_run(void);
void vm_run_cycles(unsigned int max_cycles);
void vm_stop(void);
void vm_step_one(void);
int vm_save_checkpoint(void);
int vm_restore_checkpoint(void);
uint32_t vm_state_checksum(void);
int vm_load_disk(const char *path);

#else

static inline int vm_boot(void) { (void)0; return -1; }
static inline void vm_run(void) { (void)0; }
static inline void vm_run_cycles(unsigned int n) { (void)n; }
static inline void vm_stop(void) { (void)0; }
static inline void vm_step_one(void) { (void)0; }
static inline int vm_save_checkpoint(void) { (void)0; return -1; }
static inline int vm_restore_checkpoint(void) { (void)0; return -1; }
static inline uint32_t vm_state_checksum(void) { return 0; }
static inline int vm_load_disk(const char *path) { (void)path; return -1; }

#endif

#endif /* VM_H */
