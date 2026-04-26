#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

/*
 * Spinlock primitive backed by architecture-specific ASM:
 *   x86_64: LOCK CMPXCHG acquire, XCHG release (kernel/arch/x86_64/boot/spinlock.s)
 *   AArch64: LDAXR/STXR acquire, STLR+SEV release (kernel/arch/aarch64/boot/spinlock.s)
 *   Host builds: no-op (single-threaded driver init path)
 *
 * Usage:
 *   static volatile int lock = SPINLOCK_INIT;
 *   spinlock_acquire(&lock);
 *   ... critical section ...
 *   spinlock_release(&lock);
 */

#define SPINLOCK_INIT 0

#ifdef DRIVERS_BAREMETAL
void spinlock_acquire(volatile int *lock);
void spinlock_release(volatile int *lock);
#else
/* Host: driver init runs single-threaded; stubs are sufficient. */
static inline void spinlock_acquire(volatile int *lock) { (void)lock; }
static inline void spinlock_release(volatile int *lock) { (void)lock; }
#endif

#endif /* KERNEL_SPINLOCK_H */
