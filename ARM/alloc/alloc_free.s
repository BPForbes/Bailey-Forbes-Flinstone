/* alloc_free.s - free (ARM64) */
.section .note.GNU-stack,"",%progbits
.text

.globl free

.extern lock_acquire
.extern lock_release
.extern init_heap_once_nolock
.extern push_free
.extern unlink_free

.equ HDR_SIZE, 16
.equ FLAG_FREE, 1

/* free(void* ptr) */
/* x0 = ptr */
free:
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    cbz x0, .Ldone_fast
    bl lock_acquire
    bl init_heap_once_nolock
    sub x19, x0, #HDR_SIZE  /* x19 = block header */
    ldr x0, [x19]          /* load size_and_flags */
    bic x0, x0, #1         /* clear flag to get size */
    orr x0, x0, #FLAG_FREE /* set free flag */
    str x0, [x19]          /* store back */
    mov x0, x19
    bl push_free
    ldr x0, [x19]
    bic x0, x0, #FLAG_FREE /* get size */
    add x20, x19, #HDR_SIZE
    add x20, x20, x0       /* x20 = next block */
    adrp x21, heap_end
    add x21, x21, :lo12:heap_end
    ldr x21, [x21]
    cmp x20, x21
    bhs .Lunlock_done      /* if next >= heap_end, done */
    ldr x0, [x20]          /* load next block header */
    tst x0, #FLAG_FREE
    beq .Lunlock_done      /* if not free, done */
    /* Search for next block in free list */
    mov x22, xzr           /* prev = NULL */
    adrp x21, free_head
    add x21, x21, :lo12:free_head
    ldr x21, [x21]         /* x21 = free_head */
.Lsearch:
    cbz x21, .Lunlock_done
    cmp x21, x20
    beq .Lfound
    mov x22, x21           /* prev = cur */
    ldr x21, [x21, #8]     /* cur = cur->next */
    b .Lsearch
.Lfound:
    mov x0, x22            /* prev */
    mov x1, x20            /* cur */
    bl unlink_free
    ldr x0, [x19]
    bic x0, x0, #FLAG_FREE /* get size */
    ldr x1, [x20]
    bic x1, x1, #FLAG_FREE /* get next size */
    add x0, x0, #HDR_SIZE
    add x0, x0, x1         /* total = size + HDR_SIZE + next_size */
    orr x0, x0, #FLAG_FREE
    str x0, [x19]          /* store merged size */
.Lunlock_done:
    bl lock_release
.Ldone_fast:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret
