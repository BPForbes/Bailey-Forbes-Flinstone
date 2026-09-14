BITS 16
ORG 0x7c00
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [boot_drive], dl
    mov si, disk_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    xor eax, eax
    mov edi, 0x1000
    mov ecx, 0x1000
    rep stosd
    mov dword [0x1000], 0x2003
    mov dword [0x2000], 0x3003
    mov edi, 0x3000
    mov eax, 0x83
    mov ecx, 512
.map:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map
    lgdt [gdt_ptr]
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax
    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31 | 1
    mov cr0, eax
    jmp dword 0x08:0x10000
disk_error:
    mov dx, 0x3f8
    mov al, 'E'
    out dx, al
    cli
    hlt
boot_drive: db 0
align 4
disk_packet:
    db 0x10, 0
sector_count: dw 0
    dw 0x0000, 0x1000
    dq 1
align 8
gdt:
    dq 0
    dq 0x00af9a000000ffff
    dq 0x00af92000000ffff
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt
times 510-($-$$) db 0
dw 0xaa55
