#ifndef VM_H
#define VM_H

#ifdef VM_ENABLE

int vm_boot(void);
void vm_run(void);
void vm_stop(void);

#else

static inline int vm_boot(void) { (void)0; return -1; }
static inline void vm_run(void) { (void)0; }
static inline void vm_stop(void) { (void)0; }

#endif

#endif /* VM_H */
