; alloc_malloc.asm - malloc, calloc, realloc (NASM x86-64)
section .text

global malloc
global calloc
global realloc

extern lock_acquire
extern lock_release
extern init_heap_once_nolock
extern malloc_nolock
extern free

HDR_SIZE equ 16

; malloc(size_t size) -> void*
; rdi = size
malloc:
    push rbx
    push r12
    push r13
    mov r12, rdi
    call lock_acquire
    call init_heap_once_nolock
    mov rdi, r12
    call malloc_nolock
    mov rbx, rax
    call lock_release
    mov rax, rbx
    pop r13
    pop r12
    pop rbx
    ret

; calloc(size_t nmemb, size_t size) -> void*
; rdi = nmemb, rsi = size
calloc:
    push rbx
    push r12
    push r13
    test rdi, rdi
    jz .ret0
    test rsi, rsi
    jz .ret0
    mov rax, rdi
    mul rsi
    test rdx, rdx
    jnz .ret0
    mov r13, rax
    call lock_acquire
    call init_heap_once_nolock
    mov rdi, r13
    call malloc_nolock
    mov rbx, rax
    call lock_release
    test rbx, rbx
    jz .ret0
    mov rdi, rbx
    mov rcx, r13
    xor rax, rax
    mov r12, rcx
    shr rcx, 3
    rep stosq
    mov rcx, r12
    and rcx, 7
    rep stosb
    mov rax, rbx
    pop r13
    pop r12
    pop rbx
    ret
.ret0:
    xor rax, rax
    pop r13
    pop r12
    pop rbx
    ret

; realloc(void* ptr, size_t size) -> void*
; rdi = ptr, rsi = size
realloc:
    push rbx
    push r12
    push r13
    push r14
    test rdi, rdi
    jz .realloc_malloc
    test rsi, rsi
    jz .realloc_free
    mov r12, rdi          ; ptr
    mov r13, rsi          ; new size
    mov r14, [rdi - HDR_SIZE]
    and r14, -16          ; old_payload, preserved
    call lock_acquire
    call init_heap_once_nolock
    mov rdi, r13
    call malloc_nolock
    mov rbx, rax          ; newptr
    call lock_release
    test rbx, rbx
    jz .realloc_fail
    ; copy_len = min(old_payload, new_size)
    mov rcx, r14
    cmp r14, r13
    cmova rcx, r13
    mov rdi, rbx          ; dst
    mov rsi, r12          ; src
    rep movsb
    mov rdi, r12
    call free
    mov rax, rbx
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
.realloc_malloc:
    mov rdi, rsi
    pop r14
    pop r13
    pop r12
    pop rbx
    jmp malloc
.realloc_free:
    call free
    xor rax, rax
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
.realloc_fail:
    xor rax, rax
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
