/* alloc_free.s - free (GAS/AT&T x86-64) */
.section .note.GNU-stack,"",@progbits
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
free:
    pushq %rbx
    pushq %r12
    testq %rdi, %rdi
    jz .Ldone_fast
    call lock_acquire
    call init_heap_once_nolock
    leaq -HDR_SIZE(%rdi), %rbx
    movq (%rbx), %rax
    andq $-16, %rax
    orq $FLAG_FREE, %rax
    movq %rax, (%rbx)
    movq %rbx, %rdi
    call push_free
    movq (%rbx), %rax
    andq $-16, %rax
    leaq HDR_SIZE(%rbx), %r12
    addq %rax, %r12
    movq heap_end(%rip), %rcx
    cmpq %rcx, %r12
    jae .Lunlock_done
    movq (%r12), %rdx
    testq $FLAG_FREE, %rdx
    jz .Lunlock_done
    xorq %r8, %r8
    movq free_head(%rip), %r9
.Lsearch:
    testq %r9, %r9
    jz .Lunlock_done
    cmpq %r12, %r9
    je .Lfound
    movq %r9, %r8
    movq 8(%r9), %r9
    jmp .Lsearch
.Lfound:
    movq %r8, %rdi
    movq %r12, %rsi
    call unlink_free
    movq (%rbx), %rax
    andq $-16, %rax
    movq (%r12), %rdx
    andq $-16, %rdx
    addq $HDR_SIZE, %rax
    addq %rdx, %rax
    orq $FLAG_FREE, %rax
    movq %rax, (%rbx)
.Lunlock_done:
    call lock_release
.Ldone_fast:
    popq %r12
    popq %rbx
    ret
