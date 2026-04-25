/* x86_64 GAS spinlock primitives
 *
 * spinlock_acquire(volatile int *lock)
 *   Busy-waits until *lock transitions from 0 to 1 atomically.
 *   Uses LOCK CMPXCHG: if [rdi]==0 set it to 1 and return; else spin.
 *   Memory clobber via LOCK ensures acquire semantics on x86.
 *
 * spinlock_release(volatile int *lock)
 *   Writes 0 to *lock with XCHG (implicit LOCK prefix) for release semantics.
 *
 * System V AMD64 ABI: rdi = first argument; rax used as scratch (callee may clobber).
 */

.section .note.GNU-stack,"",@progbits
.text
.globl spinlock_acquire
.globl spinlock_release

spinlock_acquire:
.Lspin_try:
    xorl    %eax, %eax          /* expected = 0 */
    movl    $1,   %ecx          /* desired  = 1 */
    lock cmpxchgl %ecx, (%rdi)  /* if *lock==0: *lock=1, ZF=1; else ZF=0 */
    jnz     .Lspin_try          /* spin until we won the exchange */
    ret

spinlock_release:
    xorl    %eax, %eax
    xchgl   %eax, (%rdi)        /* atomic store 0; implicit LOCK prefix */
    ret
