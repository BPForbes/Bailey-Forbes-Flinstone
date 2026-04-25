/* AArch64 GAS spinlock primitives
 *
 * spinlock_acquire(volatile int *lock)
 *   Uses WFE while contended, then LDAXR/STXR for LL/SC acquire semantics.
 *
 * spinlock_release(volatile int *lock)
 *   STLR (store-release) atomically writes 0, then SEV wakes waiters.
 *
 * AArch64 ABI: x0 = first argument.
 */

.section .note.GNU-stack,"",@progbits
.text
.globl spinlock_acquire
.globl spinlock_release

spinlock_acquire:
.Lspin_try:
    ldaxr   w1, [x0]        /* load-acquire exclusive: w1 = *lock */
    cbnz    w1, .Lspin_wait /* if already held, wait without exclusive churn */
    mov     w1, #1
    stxr    w2, w1, [x0]    /* try to store 1; w2=0 on success */
    cbnz    w2, .Lspin_try  /* lost the exclusive, retry */
    ret
.Lspin_wait:
    wfe
    ldr     w1, [x0]
    cbnz    w1, .Lspin_wait
    b       .Lspin_try

spinlock_release:
    stlr    wzr, [x0]       /* store-release 0 to *lock */
    sev                     /* wake waiters parked in WFE */
    ret
