BITS 64
DEFAULT REL
GLOBAL kernel_entry
EXTERN fl_kernel_main
EXTERN __bss_start
EXTERN __bss_end
SECTION .text
kernel_entry:
    cli
    cld
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    lea rsp, [stack_top]
    xor eax, eax
    lea rdi, [__bss_start]
    lea rcx, [__bss_end]
    sub rcx, rdi
    rep stosb
    call fl_kernel_main
.halt:
    cli
    hlt
    jmp .halt
SECTION .bss
align 16
stack_bottom: resb 16384
stack_top:
SECTION .note.GNU-stack noalloc noexec nowrite progbits
