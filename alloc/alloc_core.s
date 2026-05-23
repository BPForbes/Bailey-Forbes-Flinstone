/* alloc_core.s - thread-safe allocator core (GAS/AT&T x86-64)
 * BSS, lock, align16, sys_brk, free-list helpers, malloc_nolock
 * Block: [size_and_flags|next_free] 16 bytes, payload after
 * FLAG_FREE bit0 = 1
 */
.section .note.GNU-stack,"",@progbits
.text

.globl malloc_nolock
.globl init_heap_once_nolock
.globl lock_acquire
.globl lock_release
.globl push_free
.globl unlink_free

.equ SYS_brk, 12
.equ HDR_SIZE, 16
.equ FLAG_FREE, 1

/* heap_end, free_head, alloc_lock - in alloc_data.s (separate) */
.comm heap_end, 8, 8
.comm free_head, 8, 8
.comm alloc_lock, 8, 8

/* lock_acquire: spin until alloc_lock=0, then set to 1 */
lock_acquire:
    movl $1, %eax
.Lspin:
    xchgq %rax, alloc_lock(%rip)
    testq %rax, %rax
    jz .Lgot
    pause
    movl $1, %eax
    jmp .Lspin
.Lgot:
    ret

/* lock_release: alloc_lock = 0 */
lock_release:
    movq $0, alloc_lock(%rip)
    ret

/* align16(rdi) -> rax, align up to 16 */
align16:
    leaq 15(%rdi), %rax
    andq $-16, %rax
    ret

/* sys_brk(rdi) -> rax, brk(0) = query current */
sys_brk:
    movq $SYS_brk, %rax
    syscall
    ret

/* init_heap_once_nolock: set heap_end via brk(0) if not set. MUST hold lock. */
init_heap_once_nolock:
    movq heap_end(%rip), %rax
    testq %rax, %rax
    jnz .Ldone
    xorq %rdi, %rdi
    call sys_brk
    movq %rax, heap_end(%rip)
.Ldone:
    ret

/* unlink_free(rdi=prev/0 if head, rsi=cur) */
unlink_free:
    testq %rdi, %rdi
    jz .Lrm_head
    movq 8(%rsi), %rax
    movq %rax, 8(%rdi)
    ret
.Lrm_head:
    movq 8(%rsi), %rax
    movq %rax, free_head(%rip)
    ret

/* push_free(rdi=block) */
push_free:
    movq free_head(%rip), %rax
    movq %rax, 8(%rdi)
    movq %rdi, free_head(%rip)
    ret

/* malloc_nolock(rdi=size) -> rax=ptr or 0. MUST hold lock. */
malloc_nolock:
    testq %rdi, %rdi
    jz .Lret0
    call align16
    movq %rax, %r12
    xorq %r13, %r13
    movq free_head(%rip), %rbx

.Lfind_fit:
    testq %rbx, %rbx
    jz .Lno_fit
    movq (%rbx), %rax
    andq $-16, %rax
    cmpq %r12, %rax
    jb .Lnext
    movq %r13, %rdi
    movq %rbx, %rsi
    call unlink_free
    movq (%rbx), %rax
    andq $-16, %rax
    subq %r12, %rax
    cmpq $HDR_SIZE+16, %rax
    jb .Luse_whole
    leaq HDR_SIZE(%rbx), %rdx
    addq %r12, %rdx
    subq $HDR_SIZE, %rax
    movq %rax, %rcx
    orq $FLAG_FREE, %rcx
    movq %rcx, (%rdx)
    movq %rdx, %rdi
    call push_free
    movq %r12, (%rbx)
    jmp .Lreturn_ptr

.Luse_whole:
    movq (%rbx), %rax
    andq $-16, %rax
    movq %rax, (%rbx)
    jmp .Lreturn_ptr

.Lnext:
    movq %rbx, %r13
    movq 8(%rbx), %rbx
    jmp .Lfind_fit

.Lno_fit:
    movq heap_end(%rip), %rbx
    leaq HDR_SIZE(%rbx), %rdi
    addq %r12, %rdi
    call sys_brk
    cmpq %rdi, %rax
    jb .Lret0
    movq %rax, heap_end(%rip)
    movq %r12, (%rbx)

.Lreturn_ptr:
    leaq HDR_SIZE(%rbx), %rax
    ret

.Lret0:
    xorq %rax, %rax
    ret
