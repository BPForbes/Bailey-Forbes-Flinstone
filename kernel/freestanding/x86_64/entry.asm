BITS 64
DEFAULT REL
GLOBAL kernel_entry
EXTERN fl_kernel_main
EXTERN __bss_start
EXTERN __bss_end
%include "bootinfo.inc"
SECTION .text
kernel_entry:
    cli
    cld
    ; edi holds the boot-info address (below 4 GiB); ebx keeps it across the bss clear.
    mov ebx, edi
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, FL_KERNEL_STACK_TOP ; below the image; stage 2 reserves it in the boot info
    xor eax, eax
    lea rdi, [__bss_start]
    lea rcx, [__bss_end]
    sub rcx, rdi
    rep stosb
    mov edi, ebx
    call fl_kernel_main
.halt:
    cli
    hlt
    jmp .halt
SECTION .note.GNU-stack noalloc noexec nowrite progbits
