; Host shell: append one command line + LF using rep movsb (x86-64 System V ABI).
section .note.GNU-stack progbits alloc noexec
section .text

global history_asm_append_record

; size_t history_asm_append_record(char *buf, size_t cap, size_t used,
;                                  const char *cmd, size_t cmd_len);
history_asm_append_record:
    mov rax, rdx
    cmp rax, rsi
    ja .overflow
    mov r9, rsi
    sub r9, rax
    mov r10, r8
    inc r10
    cmp r9, r10
    jb .overflow

    mov r11, rdi
    add r11, rax

    mov rdi, r11
    mov rsi, rcx
    mov rcx, r8
    cld
    rep movsb

    mov byte [rdi], 10

    mov rax, rdx
    add rax, r8
    inc rax
    ret

.overflow:
    mov rax, -1
    ret
