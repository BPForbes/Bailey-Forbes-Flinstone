/* fl_stack_t and elevation slot counting (x86-64 GAS, SysV AMD64). */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_fl_stack_push
.globl asm_fl_stack_pop
.globl asm_fl_stack_count
.globl asm_fl_elev_count_active

.equ FL_STACK_OFF_DATA, 0
.equ FL_STACK_OFF_CAP,  8
.equ FL_STACK_OFF_TOP,  16
.equ FL_ELEV_STRIDE,    56
.equ FL_ELEV_OFF_EXPIRES, 40
.equ FL_ELEV_OFF_USED,  48

/* int asm_fl_stack_push(void *stack, uintptr_t v) */
asm_fl_stack_push:
    testq %rdi, %rdi
    jz .L_push_fail
    movq FL_STACK_OFF_TOP(%rdi), %rcx
    movq FL_STACK_OFF_CAP(%rdi), %rdx
    cmpq %rdx, %rcx
    jae .L_push_fail
    movq FL_STACK_OFF_DATA(%rdi), %r8
    movq %rsi, (%r8,%rcx,8)
    incq %rcx
    movq %rcx, FL_STACK_OFF_TOP(%rdi)
    xorl %eax, %eax
    ret
.L_push_fail:
    movl $-1, %eax
    ret

/* int asm_fl_stack_pop(void *stack, uintptr_t *out) */
asm_fl_stack_pop:
    testq %rdi, %rdi
    jz .L_pop_fail
    movq FL_STACK_OFF_TOP(%rdi), %rcx
    testq %rcx, %rcx
    jz .L_pop_fail
    decq %rcx
    movq FL_STACK_OFF_DATA(%rdi), %r8
    testq %rsi, %rsi
    jz .L_pop_skip_load
    movq (%r8,%rcx,8), %rdx
    movq %rdx, (%rsi)
.L_pop_skip_load:
    movq %rcx, FL_STACK_OFF_TOP(%rdi)
    xorl %eax, %eax
    ret
.L_pop_fail:
    movl $-1, %eax
    ret

/* size_t asm_fl_stack_count(const void *stack) */
asm_fl_stack_count:
    testq %rdi, %rdi
    jz .L_count_zero
    movq FL_STACK_OFF_TOP(%rdi), %rax
    ret
.L_count_zero:
    xorl %eax, %eax
    ret

/* size_t asm_fl_elev_count_active(void *slots, size_t max_slots, int64_t now_ns) */
asm_fl_elev_count_active:
    xorq %rax, %rax
    testq %rdi, %rdi
    jz .L_elev_done
    xorq %rcx, %rcx
.L_elev_loop:
    cmpq %rsi, %rcx
    jae .L_elev_done
    movq %rcx, %r8
    imulq $FL_ELEV_STRIDE, %r8, %r8
    addq %rdi, %r8
    cmpl $0, FL_ELEV_OFF_USED(%r8)
    je .L_elev_next
    movq FL_ELEV_OFF_EXPIRES(%r8), %r10
    cmpq %rdx, %r10
    jle .L_elev_expire
    incq %rax
    jmp .L_elev_next
.L_elev_expire:
    xorq %r9, %r9
.L_elev_zero_q:
    movq %r9, (%r8,%r9,8)
    addq $8, %r9
    cmpq $FL_ELEV_STRIDE, %r9
    jb .L_elev_zero_q
.L_elev_next:
    incq %rcx
    jmp .L_elev_loop
.L_elev_done:
    ret
