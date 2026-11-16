; boot_fail: shared by both stages; writes the marker to COM1 (bounded wait) and the screen, then halts.
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
