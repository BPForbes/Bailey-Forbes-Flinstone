/* alloc_malloc.s - malloc, calloc, realloc (GAS/AT&T x86-64) */
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

.equ HDR_SIZE, 16

/* malloc(size_t size) -> void* */
malloc:
    pushq %rbx
    pushq %r12
    pushq %r13
    movq %rdi, %r12
    call lock_acquire
    call init_heap_once_nolock
    movq %r12, %rdi
    call malloc_nolock
    movq %rax, %rbx
    call lock_release
    movq %rbx, %rax
    popq %r13
    popq %r12
    popq %rbx
    ret

/* calloc(size_t nmemb, size_t size) -> void* */
calloc:
    pushq %rbx
    pushq %r12
    pushq %r13
    testq %rdi, %rdi
    jz .Lret0
    testq %rsi, %rsi
    jz .Lret0
    movq %rdi, %rax
    mulq %rsi
    testq %rdx, %rdx
    jnz .Lret0
    movq %rax, %r13
    call lock_acquire
    call init_heap_once_nolock
    movq %r13, %rdi
    call malloc_nolock
    movq %rax, %rbx
    call lock_release
    testq %rbx, %rbx
    jz .Lret0
    movq %rbx, %rdi
    movq %r13, %rcx
    xorq %rax, %rax
    movq %rcx, %r12
    shrq $3, %rcx
    rep stosq
    movq %r12, %rcx
    andq $7, %rcx
    rep stosb
    movq %rbx, %rax
    popq %r13
    popq %r12
    popq %rbx
    ret
.Lret0:
    xorq %rax, %rax
    popq %r13
    popq %r12
    popq %rbx
    ret

/* realloc(void* ptr, size_t size) -> void* */
realloc:
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    testq %rdi, %rdi
    jz .Lrealloc_malloc
    testq %rsi, %rsi
    jz .Lrealloc_free
    movq %rdi, %r12          /* ptr */
    movq %rsi, %r13          /* new size */
    movq -HDR_SIZE(%rdi), %r14
    andq $-16, %r14          /* old_payload, preserved */
    call lock_acquire
    call init_heap_once_nolock
    movq %r13, %rdi
    call malloc_nolock
    movq %rax, %rbx          /* newptr */
    call lock_release
    testq %rbx, %rbx
    jz .Lrealloc_fail
    /* copy_len = min(old_payload, new_size) */
    movq %r14, %rcx
    cmpq %r13, %r14
    cmova %r13, %rcx
    movq %rbx, %rdi          /* dst */
    movq %r12, %rsi          /* src */
    rep movsb
    movq %r12, %rdi
    call free
    movq %rbx, %rax
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret
.Lrealloc_malloc:
    movq %rsi, %rdi
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    jmp malloc
.Lrealloc_free:
    call free
    xorq %rax, %rax
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret
.Lrealloc_fail:
    xorq %rax, %rax
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret
