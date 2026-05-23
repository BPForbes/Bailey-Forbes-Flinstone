; alloc_core.asm - thread-safe allocator core (NASM x86-64)
section .note.GNU-stack progbits alloc noexec
section .text
global malloc_nolock
global init_heap_once_nolock
global lock_acquire
global lock_release
global push_free
global unlink_free

SYS_brk   equ 12
HDR_SIZE  equ 16
FLAG_FREE equ 1

section .bss
global heap_end
global free_head
heap_end:   resq 1
free_head:  resq 1
alloc_lock: resq 1

section .text
lock_acquire:
    mov eax, 1
.spin:
    xchg [rel alloc_lock], rax
    test rax, rax
    jz .got
    pause
    mov eax, 1
    jmp .spin
.got:
    ret

lock_release:
    mov qword [rel alloc_lock], 0
    ret

align16:
    lea rax, [rdi+15]
    and rax, -16
    ret

sys_brk:
    mov rax, SYS_brk
    syscall
    ret

init_heap_once_nolock:
    mov rax, [rel heap_end]
    test rax, rax
    jnz .done
    xor rdi, rdi
    call sys_brk
    mov [rel heap_end], rax
.done:
    ret

unlink_free:
    test rdi, rdi
    jz .rm_head
    mov rax, [rsi+8]
    mov [rdi+8], rax
    ret
.rm_head:
    mov rax, [rsi+8]
    mov [rel free_head], rax
    ret

push_free:
    mov rax, [rel free_head]
    mov [rdi+8], rax
    mov [rel free_head], rdi
    ret

malloc_nolock:
    test rdi, rdi
    jz .ret0
    call align16
    mov r12, rax
    xor r13, r13
    mov rbx, [rel free_head]
.find_fit:
    test rbx, rbx
    jz .no_fit
    mov rax, [rbx]
    and rax, -16
    cmp rax, r12
    jb .next
    mov rdi, r13
    mov rsi, rbx
    call unlink_free
    mov rax, [rbx]
    and rax, -16
    sub rax, r12
    cmp rax, HDR_SIZE+16
    jb .use_whole
    lea rdx, [rbx+HDR_SIZE]
    add rdx, r12
    sub rax, HDR_SIZE
    mov rcx, rax
    or rcx, FLAG_FREE
    mov [rdx], rcx
    mov rdi, rdx
    call push_free
    mov [rbx], r12
    jmp .return_ptr
.use_whole:
    mov rax, [rbx]
    and rax, -16
    mov [rbx], rax
    jmp .return_ptr
.next:
    mov r13, rbx
    mov rbx, [rbx+8]
    jmp .find_fit
.no_fit:
    mov rbx, [rel heap_end]
    lea rdi, [rbx+HDR_SIZE]
    add rdi, r12
    call sys_brk
    cmp rax, rdi
    jb .ret0
    mov [rel heap_end], rax
    mov [rbx], r12
.return_ptr:
    lea rax, [rbx+HDR_SIZE]
    ret
.ret0:
    xor rax, rax
    ret
