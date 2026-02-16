/* alloc_core.s - allocator core (AArch64 GAS)
 * Same API as x86-64. brk syscall = 214 on Linux aarch64.
 */
.section .note.GNU-stack,"",@progbits
.text
.globl malloc_nolock
.globl init_heap_once_nolock
.globl lock_acquire
.globl lock_release
.globl push_free
.globl unlink_free

.equ SYS_brk, 214
.equ HDR_SIZE, 16
.equ FLAG_FREE, 1

.comm heap_end, 8, 8
.comm free_head, 8, 8
.comm alloc_lock, 8, 8

/* lock_acquire - spin until we get the lock */
lock_acquire:
    adrp    x10, alloc_lock
    add     x10, x10, :lo12:alloc_lock
.Lspin:
    ldaxr   x8, [x10]
    cbnz    x8, .Lspin_wait
    mov     x8, #1
    stlxr   w9, x8, [x10]
    cbnz    w9, .Lspin
    ret
.Lspin_wait:
    clrex
    yield
    b       .Lspin

/* lock_release */
lock_release:
    adrp    x10, alloc_lock
    add     x10, x10, :lo12:alloc_lock
    str     xzr, [x10]
    ret

/* align16(x0) -> x0 */
align16:
    add     x0, x0, #15
    and     x0, x0, #-16
    ret

/* sys_brk(x0) -> x0 */
sys_brk:
    mov     x8, #SYS_brk
    svc     #0
    ret

/* init_heap_once_nolock */
init_heap_once_nolock:
    adrp    x10, heap_end
    add     x10, x10, :lo12:heap_end
    ldr     x8, [x10]
    cbnz    x8, .Ldone
    mov     x0, #0
    bl      sys_brk
    adrp    x10, heap_end
    add     x10, x10, :lo12:heap_end
    str     x0, [x10]
.Ldone:
    ret

/* unlink_free(x0=prev, x1=cur) */
unlink_free:
    cbz     x0, .Lrm_head
    ldr     x8, [x1, #8]
    str     x8, [x0, #8]
    ret
.Lrm_head:
    ldr     x8, [x1, #8]
    adrp    x10, free_head
    add     x10, x10, :lo12:free_head
    str     x8, [x10]
    ret

/* push_free(x0=block) */
push_free:
    adrp    x10, free_head
    add     x10, x10, :lo12:free_head
    ldr     x8, [x10]
    str     x8, [x0, #8]
    str     x0, [x10]
    ret

/* malloc_nolock(x0=size) -> x0=ptr or 0 */
malloc_nolock:
    cbz     x0, .Lret0
    stp     x19, x20, [sp, #-64]!
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    str     x25, [sp, #48]
    bl      align16
    mov     x19, x0                  /* aligned size */
    adrp    x10, free_head
    add     x10, x10, :lo12:free_head
    ldr     x20, [x10]               /* free_head */
    mov     x21, xzr                 /* prev */
.Lfind_fit:
    cbz     x20, .Lno_fit
    ldr     x22, [x20]               /* block size_and_flags */
    and     x22, x22, #-16           /* size */
    cmp     x22, x19
    b.lo    .Lnext
    mov     x0, x21
    mov     x1, x20
    bl      unlink_free
    ldr     x22, [x20]
    and     x22, x22, #-16
    sub     x22, x22, x19
    cmp     x22, #(HDR_SIZE+16)
    b.lo    .Luse_whole
    add     x23, x20, #HDR_SIZE
    add     x23, x23, x19
    sub     x22, x22, #HDR_SIZE
    orr     x24, x22, #FLAG_FREE
    str     x24, [x23]
    mov     x0, x23
    bl      push_free
    str     x19, [x20]
    b       .Lreturn_ptr
.Luse_whole:
    ldr     x22, [x20]
    and     x22, x22, #-16
    str     x22, [x20]
    b       .Lreturn_ptr
.Lnext:
    mov     x21, x20
    ldr     x20, [x20, #8]
    b       .Lfind_fit
.Lno_fit:
    adrp    x10, heap_end
    add     x10, x10, :lo12:heap_end
    ldr     x20, [x10]
    add     x22, x20, #HDR_SIZE
    add     x22, x22, x19            /* requested = heap_end + HDR_SIZE + size */
    mov     x0, x22
    bl      sys_brk
    cmp     x0, x22                  /* return >= requested? */
    b.lo    .Lret0_fail
    adrp    x10, heap_end
    add     x10, x10, :lo12:heap_end
    str     x0, [x10]
    str     x19, [x20]
.Lreturn_ptr:
    add     x0, x20, #HDR_SIZE
    ldr     x25, [sp, #48]
    ldp     x23, x24, [sp, #32]
    ldp     x21, x22, [sp, #16]
    ldp     x19, x20, [sp], #64
    ret
.Lret0_fail:
    ldr     x25, [sp, #48]
    ldp     x23, x24, [sp, #32]
    ldp     x21, x22, [sp, #16]
    ldp     x19, x20, [sp], #64
.Lret0:
    mov     x0, #0
    ret
