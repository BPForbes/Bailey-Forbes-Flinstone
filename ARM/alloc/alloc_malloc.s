/* alloc_malloc.s - malloc, calloc, realloc (ARM64) */
.section .note.GNU-stack,"",%progbits
.text

.globl malloc
.globl calloc
.globl realloc

.extern lock_acquire
.extern lock_release
.extern init_heap_once_nolock
.extern malloc_nolock
.extern free

.equ HDR_SIZE, 16

/* malloc(size_t size) -> void* */
/* x0 = size */
malloc:
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x19, x0          /* save size (callee-saved) */
    bl lock_acquire
    bl init_heap_once_nolock
    mov x0, x19
    bl malloc_nolock
    mov x20, x0          /* save result */
    bl lock_release
    mov x0, x20
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret

/* calloc(size_t nmemb, size_t size) -> void* */
/* x0 = nmemb, x1 = size */
calloc:
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    cbz x0, .Lret0
    cbz x1, .Lret0
    mul x2, x0, x1        /* total = nmemb * size */
    /* Check for overflow (simplified - check if high bits set) */
    mov x19, x2
    bl lock_acquire
    bl init_heap_once_nolock
    mov x0, x19
    bl malloc_nolock
    mov x20, x0          /* save ptr */
    bl lock_release
    cbz x20, .Lret0
    /* Zero the memory */
    mov x0, x20          /* dst */
    mov x1, x19          /* size */
    mov x2, xzr          /* zero */
    /* Simple zero loop - could be optimized */
    lsr x3, x1, #3       /* n/8 */
    cbz x3, .Lcalloc_bytes
.Lcalloc_qwords:
    str xzr, [x0], #8
    sub x3, x3, #1
    cbnz x3, .Lcalloc_qwords
.Lcalloc_bytes:
    and x1, x1, #7
    cbz x1, .Lcalloc_done
.Lcalloc_byte_loop:
    strb wzr, [x0], #1
    sub x1, x1, #1
    cbnz x1, .Lcalloc_byte_loop
.Lcalloc_done:
    mov x0, x20
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret
.Lret0:
    mov x0, xzr
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret

/* realloc(void* ptr, size_t size) -> void* */
/* x0 = ptr, x1 = size */
realloc:
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    cbz x0, .Lrealloc_malloc
    cbz x1, .Lrealloc_free
    mov x19, x0          /* ptr */
    mov x20, x1          /* new size */
    ldr x21, [x0, #-HDR_SIZE] /* load old header */
    bic x21, x21, #1     /* clear flag, get old size */
    bl lock_acquire
    bl init_heap_once_nolock
    mov x0, x20
    bl malloc_nolock
    mov x22, x0          /* newptr */
    bl lock_release
    cbz x22, .Lrealloc_fail
    /* copy_len = min(old_size, new_size) */
    cmp x21, x20
    csel x23, x21, x20, lo /* x23 = min(old, new) */
    mov x0, x22          /* dst */
    mov x1, x19          /* src */
    mov x2, x23          /* len */
    /* Simple copy loop */
    lsr x3, x2, #3
    cbz x3, .Lrealloc_bytes
.Lrealloc_qwords:
    ldr x4, [x1], #8
    str x4, [x0], #8
    sub x3, x3, #1
    cbnz x3, .Lrealloc_qwords
.Lrealloc_bytes:
    and x2, x2, #7
    cbz x2, .Lrealloc_copy_done
.Lrealloc_byte_loop:
    ldrb w4, [x1], #1
    strb w4, [x0], #1
    sub x2, x2, #1
    cbnz x2, .Lrealloc_byte_loop
.Lrealloc_copy_done:
    mov x0, x19
    bl free
    mov x0, x22
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret
.Lrealloc_malloc:
    mov x0, x1
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    b malloc
.Lrealloc_free:
    bl free
    mov x0, xzr
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret
.Lrealloc_fail:
    mov x0, xzr
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret
