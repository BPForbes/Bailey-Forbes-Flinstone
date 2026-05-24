; fl_stack_t and elevation slot counting (x86-64 NASM, SysV AMD64).
section .note.GNU-stack progbits noalloc noexec nowrite
section .text
global asm_fl_stack_push
global asm_fl_stack_pop
global asm_fl_stack_count
global asm_fl_elev_count_active

%define FL_STACK_OFF_DATA 0
%define FL_STACK_OFF_CAP  8
%define FL_STACK_OFF_TOP  16
%define FL_ELEV_STRIDE    56
%define FL_ELEV_OFF_EXPIRES 40
%define FL_ELEV_OFF_USED  48

; int asm_fl_stack_push(void *stack, uintptr_t v)
asm_fl_stack_push:
    test rdi, rdi
    jz .push_fail
    mov rcx, [rdi + FL_STACK_OFF_TOP]
    mov rdx, [rdi + FL_STACK_OFF_CAP]
    cmp rcx, rdx
    jae .push_fail
    mov r8, [rdi + FL_STACK_OFF_DATA]
    mov [r8 + rcx*8], rsi
    inc rcx
    mov [rdi + FL_STACK_OFF_TOP], rcx
    xor eax, eax
    ret
.push_fail:
    mov eax, -1
    ret

; int asm_fl_stack_pop(void *stack, uintptr_t *out)
asm_fl_stack_pop:
    test rdi, rdi
    jz .pop_fail
    mov rcx, [rdi + FL_STACK_OFF_TOP]
    test rcx, rcx
    jz .pop_fail
    dec rcx
    mov r8, [rdi + FL_STACK_OFF_DATA]
    test rsi, rsi
    jz .pop_skip_load
    mov rdx, [r8 + rcx*8]
    mov [rsi], rdx
.pop_skip_load:
    mov [rdi + FL_STACK_OFF_TOP], rcx
    xor eax, eax
    ret
.pop_fail:
    mov eax, -1
    ret

; size_t asm_fl_stack_count(const void *stack)
asm_fl_stack_count:
    test rdi, rdi
    jz .count_zero
    mov rax, [rdi + FL_STACK_OFF_TOP]
    ret
.count_zero:
    xor eax, eax
    ret

; size_t asm_fl_elev_count_active(void *slots, size_t max_slots, int64_t now_ns)
asm_fl_elev_count_active:
    xor rax, rax
    test rdi, rdi
    jz .elev_done
    xor rcx, rcx
.elev_loop:
    cmp rcx, rsi
    jae .elev_done
    mov r8, rcx
    imul r8, FL_ELEV_STRIDE
    add r8, rdi
    cmp dword [r8 + FL_ELEV_OFF_USED], 0
    je .elev_next
    mov r10, [r8 + FL_ELEV_OFF_EXPIRES]
    cmp r10, rdx
    jle .elev_expire
    inc rax
    jmp .elev_next
.elev_expire:
    xor r9, r9
.elev_zero_q:
    mov qword [r8 + r9], 0
    add r9, 8
    cmp r9, FL_ELEV_STRIDE
    jb .elev_zero_q
.elev_next:
    inc rcx
    jmp .elev_loop
.elev_done:
    ret
