BITS 16
ORG 0x7c00
start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [boot_drive], dl

    ; Chunked BIOS extended read: load total_sectors sectors starting at
    ; LBA 1 into 0x1000:0x0000 and upward, in <=127-sector pieces. A single
    ; real-mode segment spans 64 KiB (128 sectors); capping each transfer at
    ; 127 keeps it inside one segment with no destination wraparound. The
    ; destination segment advances by (sectors_this_chunk * 32) paragraphs
    ; after each read, so the payload is no longer limited to one BIOS
    ; transfer. Every register the BIOS call might clobber is reloaded from
    ; memory afterward rather than assumed to survive the call.
    mov ax, [total_sectors]
    mov [remaining], ax
.load_loop:
    mov ax, [remaining]
    test ax, ax
    jz .load_done
    cmp ax, 127
    jbe .have_chunk
    mov ax, 127
.have_chunk:
    mov [disk_packet_count], ax
    mov cx, [cur_seg]
    mov [disk_packet_seg], cx
    mov cx, [cur_lba]
    mov [disk_packet_lba], cx
    mov si, disk_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    mov ax, [disk_packet_count]
    mov cx, [remaining]
    sub cx, ax
    mov [remaining], cx
    mov cx, [cur_lba]
    add cx, ax
    mov [cur_lba], cx
    mov cx, ax
    shl cx, 5
    add cx, [cur_seg]
    mov [cur_seg], cx
    jmp .load_loop
.load_done:

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
remaining: dw 0
cur_seg: dw 0x1000
cur_lba: dw 1
align 4
disk_packet:
    db 0x10, 0
disk_packet_count: dw 0
    dw 0x0000
disk_packet_seg: dw 0x1000
disk_packet_lba: dq 0
; total_sectors is patched by build_freestanding_browser_kernel.sh. The two
; sentinel words around it make the patch site a unique byte sequence to
; locate inside the ~512-byte boot sector (same technique the old single-
; chunk loader used for its sector_count field).
align 2
total_marker_lo: dw 0xbeef
total_sectors: dw 0
total_marker_hi: dw 0xf00d
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
