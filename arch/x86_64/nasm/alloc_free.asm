; alloc_free.asm - free (NASM x86-64)
section .note.GNU-stack progbits alloc noexec
section .text
global free

extern lock_acquire
extern lock_release
extern init_heap_once_nolock
extern push_free
extern unlink_free

HDR_SIZE  equ 16
FLAG_FREE equ 1

extern heap_end
extern free_head

free:
    push rbx
    push r12
    test rdi, rdi
    jz .done_fast
    call lock_acquire
    call init_heap_once_nolock
    lea rbx, [rdi-HDR_SIZE]
    mov rax, [rbx]
    and rax, -16
    or rax, FLAG_FREE
    mov [rbx], rax
    mov rdi, rbx
    call push_free
    mov rax, [rbx]
    and rax, -16
    lea r12, [rbx+HDR_SIZE]
    add r12, rax
    mov rcx, [rel heap_end]
    cmp r12, rcx
    jae .unlock_done
    mov rdx, [r12]
    test rdx, FLAG_FREE
    jz .unlock_done
    xor r8, r8
    mov r9, [rel free_head]
.search:
    test r9, r9
    jz .unlock_done
    cmp r12, r9
    je .found
    mov r8, r9
    mov r9, [r9+8]
    jmp .search
.found:
    mov rdi, r8
    mov rsi, r12
    call unlink_free
    mov rax, [rbx]
    and rax, -16
    mov rdx, [r12]
    and rdx, -16
    add rax, HDR_SIZE
    add rax, rdx
    or rax, FLAG_FREE
    mov [rbx], rax
.unlock_done:
    call lock_release
.done_fast:
    pop r12
    pop rbx
    ret
