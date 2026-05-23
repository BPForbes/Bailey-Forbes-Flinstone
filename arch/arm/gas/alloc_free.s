/* alloc_free.s - free (AArch64 GAS) */
.section .note.GNU-stack,"",@progbits
.text
.globl free

.extern lock_acquire
.extern lock_release
.extern init_heap_once_nolock
.extern push_free
.extern unlink_free
.extern heap_end
.extern free_head

.equ HDR_SIZE, 16
.equ FLAG_FREE, 1

free:
    str     x30, [sp, #-48]!
    stp     x19, x20, [sp, #16]
    cbz     x0, .Ldone_fast
    bl      lock_acquire
    bl      init_heap_once_nolock
    sub     x19, x0, #HDR_SIZE       /* block = ptr - HDR_SIZE */
    ldr     x20, [x19]
    and     x20, x20, #-16
    orr     x20, x20, #FLAG_FREE
    str     x20, [x19]
    mov     x0, x19
    bl      push_free
    ldr     x20, [x19]
    and     x20, x20, #-16
    add     x21, x19, #HDR_SIZE
    add     x21, x21, x20            /* next = block + HDR_SIZE + size */
    adrp    x10, heap_end
    add     x10, x10, :lo12:heap_end
    ldr     x22, [x10]
    cmp     x21, x22
    b.hs    .Lunlock_done
    ldr     x23, [x21]
    tst     x23, #FLAG_FREE
    b.eq    .Lunlock_done
    mov     x24, xzr                /* prev */
    adrp    x10, free_head
    add     x10, x10, :lo12:free_head
    ldr     x25, [x10]              /* p = free_head */
.Lsearch:
    cbz     x25, .Lunlock_done
    cmp     x21, x25
    b.eq    .Lfound
    mov     x24, x25
    ldr     x25, [x25, #8]
    b       .Lsearch
.Lfound:
    mov     x0, x24
    mov     x1, x21
    bl      unlink_free
    ldr     x20, [x19]
    and     x20, x20, #-16
    ldr     x23, [x21]
    and     x23, x23, #-16
    add     x20, x20, #HDR_SIZE
    add     x20, x20, x23
    orr     x20, x20, #FLAG_FREE
    str     x20, [x19]
.Lunlock_done:
    bl      lock_release
.Ldone_fast:
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #48
    ret
