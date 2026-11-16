; Stage 1: the BIOS boot sector. Reads the bounded stage 2 (FL_STAGE2_SECTORS sectors from LBA 1) to
; FL_STAGE2_ADDR, checks the read and stage 2's trailing magic, and jumps to it with the drive number
; in dl. Stage 2 (stage2.asm) does the rest.
BITS 16
ORG 0x7c00
%include "bootinfo.inc"
start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, FL_STAGE1_ADDR
    sti
    mov [boot_drive], dl

    mov ah, 0x41
    mov bx, 0x55aa
    int 0x13
    mov si, msg_lba ; after the call: a BIOS may change si
    jc boot_fail
    cmp bx, 0xaa55
    jne boot_fail
    test cl, 1
    jz boot_fail

    mov word [disk_packet_count], FL_STAGE2_SECTORS
    mov si, disk_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    mov si, msg_read
    jc boot_fail
    test ah, ah
    jnz boot_fail
    cmp word [disk_packet_count], FL_STAGE2_SECTORS ; the BIOS writes back what it read
    jne boot_fail
    mov si, msg_magic
    cmp dword [FL_STAGE2_ADDR + FL_STAGE2_SECTORS * 512 - 4], FL_STAGE2_MAGIC
    jne boot_fail
    mov dl, [boot_drive]
    jmp 0:FL_STAGE2_ADDR

%include "bootfail.inc"

msg_lba: db "FLINTSTONE_BOOT_FAIL lba", 13, 10, 0
msg_read: db "FLINTSTONE_BOOT_FAIL stage2-read", 13, 10, 0
msg_magic: db "FLINTSTONE_BOOT_FAIL stage2-magic", 13, 10, 0
boot_drive: db 0
align 4
disk_packet:
    db 0x10, 0
disk_packet_count: dw 0
    dw FL_STAGE2_ADDR, 0
    dq 1
times 510-($-$$) db 0
dw 0xaa55
