; Stage 2 (loaded at FL_STAGE2_ADDR by boot.asm): E820, A20, kernel load, boot info, long mode; failures halt.
%include "bootinfo.inc"
BITS 16
ORG FL_STAGE2_ADDR
%define E820_SMAP 0x534d4150
%define E820_MAX_CALLS 160 ; a BIOS that never ends its list fails the boot instead of hanging it
%define KERNEL_MAX_SECTORS ((FL_LOW_BUDGET - FL_KERNEL_LOAD_ADDR) / 512)
%define KERNEL_LBA (1 + FL_STAGE2_SECTORS)

stage2:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, FL_STAGE1_ADDR
    sti
    mov [drive], dl

    mov di, FL_BOOTINFO_ADDR
    mov cx, (FL_PAGE_TABLES - FL_BOOTINFO_ADDR) / 2
    xor ax, ax
    rep stosw

; A call past a full buffer lands in a spare slot: a valid entry there means the map did not fit (refused).
    mov si, msg_e820
    xor ebx, ebx
    mov word [e820_di], FL_BOOTINFO_E820_ADDR
.e820_next:
    inc word [e820_calls]
    cmp word [e820_calls], E820_MAX_CALLS
    ja boot_fail
    mov di, [e820_di]
    cmp word [FL_BOOTINFO_ADDR + BI_E820_COUNT], FL_BOOTINFO_E820_CAP
    jb .e820_slot
    mov di, e820_spare
.e820_slot:
    mov dword [di + 20], 1 ; ACPI 3.0 "use this entry", for a BIOS that returns only 20 bytes
    mov eax, 0xe820
    mov edx, E820_SMAP
    mov ecx, 24
    int 0x15
    mov si, msg_e820
    jc .e820_carry
    cmp eax, E820_SMAP
    jne boot_fail
    cmp ecx, 20
    jb .e820_skip ; too short to be an entry: dropped, the slot is reused
    cmp word [FL_BOOTINFO_ADDR + BI_E820_COUNT], FL_BOOTINFO_E820_CAP
    jae .e820_truncated
    inc word [FL_BOOTINFO_ADDR + BI_E820_COUNT]
    add word [e820_di], 24
.e820_skip:
    test ebx, ebx
    jnz .e820_next
    jmp .e820_done
.e820_truncated:
    or dword [FL_BOOTINFO_ADDR + BI_FLAGS], FL_BOOTINFO_F_E820_TRUNCATED ; the kernel rejects it
    jmp .e820_done
.e820_carry: ; carry on the first call: no E820. Later: the end of the list.
    cmp word [FL_BOOTINFO_ADDR + BI_E820_COUNT], 0
    je boot_fail
.e820_done:
    cmp word [FL_BOOTINFO_ADDR + BI_E820_COUNT], 0
    je boot_fail

; A20 order: BIOS, keyboard controller, then port 0x92 (side effects on some boards).
    call a20_off
    jnc .a20_on
    mov ax, 0x2401 ; BIOS
    int 0x15
    call a20_wait
    jnc .a20_on
    cli ; the BIOS keyboard handler must not read port 0x60 between the command and its data
    call kbc_wait ; keyboard controller: write the output port with A20 set
    mov al, 0xd1
    out 0x64, al
    call kbc_wait
    mov al, 0xdf
    out 0x60, al
    call kbc_wait
    sti
    call a20_wait
    jnc .a20_on
    in al, 0x92 ; fast A20 (bit 0 would reset the machine: kept clear)
    or al, 2
    and al, 0xfe
    out 0x92, al
    call a20_wait
    jnc .a20_on
    mov si, msg_a20
    jmp boot_fail
.a20_on:

    mov si, msg_size
    mov eax, [patch_total_sectors]
    test eax, eax
    jz boot_fail
    cmp eax, KERNEL_MAX_SECTORS
    ja boot_fail
    mov [remaining], ax
    mov eax, [patch_kernel_end]
    cmp eax, FL_KERNEL_LOAD_ADDR
    jbe boot_fail
    cmp eax, FL_LOW_BUDGET
    ja boot_fail
    ; Chunks of at most 127 sectors keep each read inside one 64 KiB real-mode segment.
.load_next:
    mov ax, [remaining]
    test ax, ax
    jz .load_done
    cmp ax, 127
    jbe .load_chunk
    mov ax, 127
.load_chunk:
    mov [chunk], ax
    mov [packet_count], ax
    mov cx, [cur_seg]
    mov [packet_seg], cx
    mov ecx, [cur_lba]
    mov [packet_lba], ecx
    mov si, packet
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    mov si, msg_read
    jc boot_fail
    test ah, ah
    jnz boot_fail
    mov ax, [chunk]
    cmp [packet_count], ax ; the BIOS writes back what it read
    jne boot_fail
    sub [remaining], ax
    movzx ecx, ax
    add [cur_lba], ecx
    shl ax, 5
    add [cur_seg], ax
    jmp .load_next
.load_done:

    mov dword [FL_BOOTINFO_ADDR + BI_MAGIC], FL_BOOTINFO_MAGIC
    mov word [FL_BOOTINFO_ADDR + BI_VERSION], FL_BOOTINFO_VERSION
    mov word [FL_BOOTINFO_ADDR + BI_SIZE], FL_BOOTINFO_SIZE
    movzx eax, byte [drive]
    mov [FL_BOOTINFO_ADDR + BI_BOOT_DRIVE], eax
    mov dword [FL_BOOTINFO_ADDR + BI_E820_ADDR], FL_BOOTINFO_E820_ADDR
    mov dword [FL_BOOTINFO_ADDR + BI_E820_CAPACITY], FL_BOOTINFO_E820_CAP
    mov dword [FL_BOOTINFO_ADDR + BI_KERNEL_BASE], FL_KERNEL_LOAD_ADDR
    mov eax, [patch_total_sectors]
    shl eax, 9
    add eax, FL_KERNEL_LOAD_ADDR
    mov [FL_BOOTINFO_ADDR + BI_KERNEL_LOAD_END], eax
    mov eax, [patch_kernel_end]
    mov [FL_BOOTINFO_ADDR + BI_KERNEL_END], eax
    mov [res_kernel_end], eax
    mov dword [FL_BOOTINFO_ADDR + BI_MAPPED_END], FL_IDENTITY_MAP_END
    mov dword [FL_BOOTINFO_ADDR + BI_RESERVED_COUNT], (reserved_end - reserved) / 16
    mov si, reserved
    mov di, FL_BOOTINFO_ADDR + BI_RESERVED
    mov cx, reserved_end - reserved
    rep movsb

    cli
    xor eax, eax
    mov edi, FL_PAGE_TABLES
    mov ecx, (FL_PAGE_TABLES_END - FL_PAGE_TABLES) / 4
    rep stosd
    mov dword [FL_PAGE_TABLES], FL_PAGE_TABLES + 0x1003
    mov dword [FL_PAGE_TABLES + 0x1000], FL_PAGE_TABLES + 0x2003
    mov edi, FL_PAGE_TABLES + 0x2000
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
    mov eax, FL_PAGE_TABLES
    mov cr3, eax
    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31 | 1
    mov cr0, eax
    mov edi, FL_BOOTINFO_ADDR ; entry.asm keeps it across the bss clear
    jmp dword 0x08:FL_KERNEL_LOAD_ADDR

; Carry set when A20 is off (a byte below 1 MiB aliases 1 MiB higher); 0xa5 at FL_A20_PROBE + 1 MiB stays when A20 is on.
a20_off:
    push ds
    push es
    xor ax, ax
    mov ds, ax
    dec ax
    mov es, ax ; 0xffff:(FL_A20_PROBE + 0x10) is FL_A20_PROBE + 1 MiB
    mov byte [ds:FL_A20_PROBE], 0x00
    mov byte [es:FL_A20_PROBE + 0x10], 0xff
    cmp byte [ds:FL_A20_PROBE], 0xff
    je .off
    mov byte [ds:FL_A20_PROBE], 0x5a
    mov byte [es:FL_A20_PROBE + 0x10], 0xa5
    cmp byte [ds:FL_A20_PROBE], 0xa5
    je .off
    mov byte [ds:FL_A20_PROBE], 0
    clc
    jmp .out
.off:
    mov byte [ds:FL_A20_PROBE], 0
    stc
.out:
    pop es
    pop ds
    ret

a20_wait:
    mov cx, 0x100
.again:
    push cx
    call a20_off
    pop cx
    jnc .done
    loop .again
    stc
.done:
    ret

kbc_wait:
    mov cx, 0xffff
.poll:
    in al, 0x64
    test al, 2
    loopnz .poll
    ret

%include "bootfail.inc"

msg_e820: db "FLINTSTONE_BOOT_FAIL e820", 13, 10, 0
msg_a20: db "FLINTSTONE_BOOT_FAIL a20", 13, 10, 0
msg_size: db "FLINTSTONE_BOOT_FAIL kernel-size", 13, 10, 0
msg_read: db "FLINTSTONE_BOOT_FAIL kernel-read", 13, 10, 0
drive: db 0
e820_calls: dw 0
e820_di: dw 0
align 4
e820_spare: times 24 db 0 ; the probe slot once the buffer is full
remaining: dw 0
chunk: dw 0
cur_seg: dw FL_KERNEL_LOAD_ADDR >> 4
align 4
cur_lba: dd KERNEL_LBA
packet:
    db 0x10, 0
packet_count: dw 0
    dw 0
packet_seg: dw 0
packet_lba: dq 0

align 8
reserved:
    dq 0, FL_BOOTINFO_ADDR                              ; IVT and BIOS data area
    dq FL_BOOTINFO_ADDR, FL_PAGE_TABLES                 ; boot info and E820 buffer
    dq FL_PAGE_TABLES, FL_PAGE_TABLES_END               ; page tables
    dq FL_LOADER_STACK_BOTTOM, FL_STAGE2_ADDR + FL_STAGE2_SECTORS * 512 ; loader stack, stages 1 and 2
    dq FL_KERNEL_STACK_BOTTOM, FL_KERNEL_STACK_TOP      ; kernel stack
    dq FL_KERNEL_LOAD_ADDR
res_kernel_end: dq 0                                    ; image and bss
    dq 0xa0000, 0x100000                                ; legacy video and BIOS ROM
reserved_end:

align 8
gdt:
    dq 0
    dq 0x00af9a000000ffff
    dq 0x00af92000000ffff
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt

; Patched by build_freestanding_browser_kernel.sh after the kernel is linked (found by its tag).
align 4
patch_tag: db "FLS2PTCH"
patch_total_sectors: dd 0
patch_kernel_end: dd 0

times FL_STAGE2_SECTORS * 512 - 4 - ($ - $$) db 0
    dd FL_STAGE2_MAGIC
