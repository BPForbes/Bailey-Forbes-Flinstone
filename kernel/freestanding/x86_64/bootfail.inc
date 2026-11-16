; boot_fail (FL-02): the loader's one failure path, shared by stage 1 and stage 2. ds:si points at
; a NUL-terminated marker such as "FLINTSTONE_BOOT_FAIL a20". It is written to COM1 (waiting a
; bounded time for the transmitter, so a missing UART cannot hang it) and to the screen through
; the BIOS, then the CPU halts for good.
boot_fail:
    cld
.next:
    lodsb
    test al, al
    jz .halt
    mov [boot_fail_ch], al
    mov dx, 0x3fd
    mov cx, 0xffff
.wait:
    in al, dx
    test al, 0x20
    loopz .wait
    mov dx, 0x3f8
    mov al, [boot_fail_ch]
    out dx, al
    push si
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    pop si
    jmp .next
.halt:
    cli
    hlt
    jmp .halt
boot_fail_ch: db 0
