/* alloc_core.s - thread-safe allocator core (ARM64)
 * BSS, lock, align16, sys_brk, free-list helpers, malloc_nolock
 * Block: [size_and_flags|next_free] 16 bytes, payload after
 * FLAG_FREE bit0 = 1
 */
.section .note.GNU-stack,"",%progbits
.text

.globl malloc_nolock
.globl init_heap_once_nolock
.globl lock_acquire
.globl lock_release
.globl push_free
.globl unlink_free

.equ SYS_brk, 214        /* ARM64 syscall number for brk */
.equ HDR_SIZE, 16
.equ FLAG_FREE, 1

/* heap_end, free_head, alloc_lock - in BSS */
.comm heap_end, 8, 8
.comm free_head, 8, 8
.comm alloc_lock, 8, 8

/* lock_acquire: spin until alloc_lock=0, then set to 1 */
lock_acquire:
    mov w1, #1
.Lspin:
    adrp x2, alloc_lock
    add x2, x2, :lo12:alloc_lock
    mov w0, #1
    swpal w0, w1, [x2]   /* atomic swap: w0 = old, [x2] = w1 */
    cbz w0, .Lgot         /* if old was 0, we got the lock */
    /* pause/yield hint for ARM */
    yield
    mov w1, #1
    b .Lspin
.Lgot:
    ret

/* lock_release: alloc_lock = 0 */
lock_release:
    adrp x0, alloc_lock
    add x0, x0, :lo12:alloc_lock
    str xzr, [x0]         /* store zero */
    ret

/* align16(x0) -> x0, align up to 16 */
align16:
    add x0, x0, #15
    bic x0, x0, #15        /* clear low 4 bits */
    ret

/* sys_brk(x0) -> x0, brk(0) = query current */
sys_brk:
    mov x8, #SYS_brk      /* syscall number */
    svc #0                /* system call */
    ret

/* init_heap_once_nolock: set heap_end via brk(0) if not set. MUST hold lock. */
init_heap_once_nolock:
    adrp x0, heap_end
    add x0, x0, :lo12:heap_end
    ldr x1, [x0]
    cbnz x1, .Ldone       /* if already set, done */
    mov x0, xzr           /* brk(0) to query */
    bl sys_brk
    adrp x1, heap_end
    add x1, x1, :lo12:heap_end
    str x0, [x1]
.Ldone:
    ret

/* unlink_free(x0=prev/0 if head, x1=cur) */
unlink_free:
    cbnz x0, .Lrm_mid     /* if prev != 0, not head */
    /* Remove from head */
    ldr x2, [x1, #8]      /* load cur->next */
    adrp x0, free_head
    add x0, x0, :lo12:free_head
    str x2, [x0]
    ret
.Lrm_mid:
    ldr x2, [x1, #8]      /* load cur->next */
    str x2, [x0, #8]      /* store to prev->next */
    ret

/* push_free(x0=block) */
push_free:
    adrp x1, free_head
    add x1, x1, :lo12:free_head
    ldr x2, [x1]          /* load current head */
    str x2, [x0, #8]      /* store as block->next */
    str x0, [x1]          /* store block as new head */
    ret

/* malloc_nolock(x0=size) -> x0=ptr or 0. MUST hold lock. */
malloc_nolock:
    cbz x0, .Lret0
    bl align16
    mov x19, x0           /* save aligned size (callee-saved) */
    mov x20, xzr          /* prev = NULL (callee-saved) */
    adrp x21, free_head
    add x21, x21, :lo12:free_head
    ldr x21, [x21]        /* x21 = free_head (callee-saved) */

.Lfind_fit:
    cbz x21, .Lno_fit
    ldr x0, [x21]         /* load block->size_and_flags */
    bic x0, x0, #FLAG_FREE /* clear flag, get size */
    cmp x0, x19
    blo .Lnext            /* if size < needed, next */
    mov x0, x20           /* prev */
    mov x1, x21           /* cur */
    bl unlink_free
    ldr x0, [x21]         /* load size */
    bic x0, x0, #FLAG_FREE
    sub x0, x0, x19       /* remaining = size - needed */
    cmp x0, #(HDR_SIZE + 16)
    blo .Luse_whole
    /* Split block */
    add x22, x21, #HDR_SIZE
    add x22, x22, x19     /* x22 = split point */
    sub x0, x0, #HDR_SIZE /* adjust remaining */
    orr x0, x0, #FLAG_FREE
    str x0, [x22]         /* store size_and_flags for split */
    mov x0, x22
    bl push_free
    str x19, [x21]        /* store size (no flag) */
    b .Lreturn_ptr

.Luse_whole:
    ldr x0, [x21]
    bic x0, x0, #FLAG_FREE
    str x0, [x21]         /* clear flag */
    b .Lreturn_ptr

.Lnext:
    mov x20, x21          /* prev = cur */
    ldr x21, [x21, #8]    /* cur = cur->next */
    b .Lfind_fit

.Lno_fit:
    adrp x0, heap_end
    add x0, x0, :lo12:heap_end
    ldr x22, [x0]         /* x22 = heap_end */
    add x0, x22, #HDR_SIZE
    add x0, x0, x19       /* new_brk = heap_end + HDR_SIZE + size */
    bl sys_brk
    adrp x1, heap_end
    add x1, x1, :lo12:heap_end
    ldr x1, [x1]
    add x1, x1, #HDR_SIZE
    add x1, x1, x19
    cmp x0, x1
    blo .Lret0            /* if brk failed */
    adrp x0, heap_end
    add x0, x0, :lo12:heap_end
    str x22, [x0]         /* update heap_end */
    str x19, [x22]        /* store size */

.Lreturn_ptr:
    add x0, x22, #HDR_SIZE
    ret

.Lret0:
    mov x0, xzr
    ret
