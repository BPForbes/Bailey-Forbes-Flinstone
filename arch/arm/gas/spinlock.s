/* AArch64 GAS spinlock primitives
 *
 * spinlock_acquire(volatile int *lock)
 *   Uses LDXR/STXR exclusive monitor loop for LL/SC acquire semantics.
 *   DMB ISH after acquisition ensures subsequent loads/stores are ordered.
 *
 * spinlock_release(volatile int *lock)
 *   DMB ISH before store ensures prior writes are visible, then STLR
 *   (store-release) atomically writes 0.
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
    cbnz    w1, .Lspin_try  /* if already held, spin */
    mov     w1, #1
    stxr    w2, w1, [x0]    /* try to store 1; w2=0 on success */
    cbnz    w2, .Lspin_try  /* lost the exclusive, retry */
    dmb     ish             /* acquire barrier */
    ret

spinlock_release:
    dmb     ish             /* release barrier: drain pending writes */
    stlr    wzr, [x0]       /* store-release 0 to *lock */
    ret
