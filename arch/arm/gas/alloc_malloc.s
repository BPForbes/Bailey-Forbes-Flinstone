/* alloc_malloc.s - malloc, calloc, realloc (AArch64 GAS) */
.section .note.GNU-stack,"",@progbits
.text
.globl malloc
.globl calloc
.globl realloc

.extern lock_acquire
.extern lock_release
.extern init_heap_once_nolock
.extern malloc_nolock
.extern free
.extern asm_mem_zero
.extern asm_mem_copy

.equ HDR_SIZE, 16

malloc:
    str     x30, [sp, #-48]!
    stp     x19, x20, [sp, #16]
    mov     x19, x0
    bl      lock_acquire
    bl      init_heap_once_nolock
    mov     x0, x19
    bl      malloc_nolock
    mov     x20, x0
    bl      lock_release
    mov     x0, x20
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #48
    ret

calloc:
    str     x30, [sp, #-48]!
    stp     x19, x20, [sp, #16]
    cbz     x0, .Lret0
    cbz     x1, .Lret0
    mul     x19, x0, x1
    umulh   x8, x0, x1
    cbnz    x8, .Lret0
    bl      lock_acquire
    bl      init_heap_once_nolock
    mov     x0, x19
    bl      malloc_nolock
    mov     x20, x0
    bl      lock_release
    cbz     x20, .Lret0
    mov     x0, x20
    mov     x1, x19
    bl      asm_mem_zero
    mov     x0, x20
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #48
    ret
.Lret0:
    mov     x0, #0
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #48
    ret

realloc:
    str     x30, [sp, #-64]!
    stp     x19, x20, [sp, #16]
    stp     x21, x22, [sp, #32]
    cbz     x0, .Lrealloc_malloc
    cbz     x1, .Lrealloc_free
    mov     x19, x0
    mov     x20, x1
    ldr     x21, [x0, #-HDR_SIZE]
    and     x21, x21, #-16
    bl      lock_acquire
    bl      init_heap_once_nolock
    mov     x0, x20
    bl      malloc_nolock
    mov     x22, x0
    bl      lock_release
    cbz     x22, .Lrealloc_fail
    cmp     x21, x20
    csel    x2, x21, x20, lo
    mov     x0, x22
    mov     x1, x19
    bl      asm_mem_copy
    mov     x0, x19
    bl      free
    mov     x0, x22
    ldp     x21, x22, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #64
    ret
.Lrealloc_malloc:
    mov     x0, x1
    ldp     x21, x22, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #64
    b       malloc
.Lrealloc_free:
    bl      free
    mov     x0, #0
    ldp     x21, x22, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #64
    ret
.Lrealloc_fail:
    mov     x0, #0
    ldp     x21, x22, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #64
    ret
