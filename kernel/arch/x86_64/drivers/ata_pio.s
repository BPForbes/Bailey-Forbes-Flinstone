/* ATA PIO 28-bit LBA sector I/O - x86-64 GAS
 *
 * Implements raw 512-byte sector reads and writes via the primary ATA
 * (IDE) channel (I/O ports 0x1F0-0x1F7).  Used by disk_asm.c when
 * DRIVERS_BAREMETAL is defined.
 *
 * Returns int in %eax: 0 on success, -1 on timeout or ATA error (ERR/DF).
 *
 * System V AMD64 ABI:
 *   int ata_pio_read_sector (uint32_t lba, void *buf)
 *   int ata_pio_write_sector(uint32_t lba, const void *buf)
 */

.equ ATA_DATA,     0x1F0
.equ ATA_SECCOUNT, 0x1F2
.equ ATA_LBA_LO,   0x1F3
.equ ATA_LBA_MID,  0x1F4
.equ ATA_LBA_HI,   0x1F5
.equ ATA_DRIVE,    0x1F6
.equ ATA_STATCMD,  0x1F7

.equ ATA_CMD_READ,   0x20
.equ ATA_CMD_WRITE,  0x30
.equ ATA_CMD_FLUSH,  0xE7

.equ ATA_SR_BSY,  0x80
.equ ATA_SR_DRQ,  0x08
.equ ATA_SR_ERR,  0x01
.equ ATA_SR_DF,   0x20

/* ~8M status polls — bounded wait if drive missing or hung */
.equ ATA_POLL_MAX, 0x007A1200

.section .note.GNU-stack,"",@progbits
.text
.globl ata_pio_read_sector
.globl ata_pio_write_sector

/* ------------------------------------------------------------------ */
/* int ata_pio_read_sector(uint32_t lba, void *buf)                     */
/* ------------------------------------------------------------------ */
ata_pio_read_sector:
    push    %rbx
    push    %r12
    push    %r13
    movl    %edi, %ebx
    movq    %rsi, %r12

    movl    $ATA_POLL_MAX, %r13d
.Lrd_wait_bsy:
    decl    %r13d
    jz      .Lrd_fail
    movl    $ATA_STATCMD, %edx
    inb     %dx, %al
    testb   $ATA_SR_ERR, %al
    jnz     .Lrd_fail
    testb   $ATA_SR_DF, %al
    jnz     .Lrd_fail
    testb   $ATA_SR_BSY, %al
    jnz     .Lrd_wait_bsy

    movl    $ATA_DRIVE, %edx
    movl    %ebx, %eax
    shrl    $24, %eax
    andl    $0x0F, %eax
    orl     $0xE0, %eax
    outb    %al, %dx

    movl    $ATA_SECCOUNT, %edx
    movb    $1, %al
    outb    %al, %dx

    movl    $ATA_LBA_LO, %edx
    movl    %ebx, %eax
    outb    %al, %dx

    movl    $ATA_LBA_MID, %edx
    movl    %ebx, %eax
    shrl    $8, %eax
    outb    %al, %dx

    movl    $ATA_LBA_HI, %edx
    movl    %ebx, %eax
    shrl    $16, %eax
    outb    %al, %dx

    movl    $ATA_STATCMD, %edx
    movb    $ATA_CMD_READ, %al
    outb    %al, %dx

    movl    $ATA_POLL_MAX, %r13d
.Lrd_wait_drq:
    decl    %r13d
    jz      .Lrd_fail
    movl    $ATA_STATCMD, %edx
    inb     %dx, %al
    testb   $ATA_SR_ERR, %al
    jnz     .Lrd_fail
    testb   $ATA_SR_DF, %al
    jnz     .Lrd_fail
    testb   $ATA_SR_BSY, %al
    jnz     .Lrd_wait_drq
    testb   $ATA_SR_DRQ, %al
    jz      .Lrd_wait_drq

    movl    $ATA_DATA, %edx
    movq    %r12, %rdi
    movl    $256, %ecx
    rep insw

    xorl    %eax, %eax
    pop     %r13
    pop     %r12
    pop     %rbx
    ret

.Lrd_fail:
    movl    $-1, %eax
    pop     %r13
    pop     %r12
    pop     %rbx
    ret

/* ------------------------------------------------------------------ */
/* int ata_pio_write_sector(uint32_t lba, const void *buf)            */
/* ------------------------------------------------------------------ */
ata_pio_write_sector:
    push    %rbx
    push    %r12
    push    %r13
    movl    %edi, %ebx
    movq    %rsi, %r12

    movl    $ATA_POLL_MAX, %r13d
.Lwr_wait_bsy:
    decl    %r13d
    jz      .Lwr_fail
    movl    $ATA_STATCMD, %edx
    inb     %dx, %al
    testb   $ATA_SR_ERR, %al
    jnz     .Lwr_fail
    testb   $ATA_SR_DF, %al
    jnz     .Lwr_fail
    testb   $ATA_SR_BSY, %al
    jnz     .Lwr_wait_bsy

    movl    $ATA_DRIVE, %edx
    movl    %ebx, %eax
    shrl    $24, %eax
    andl    $0x0F, %eax
    orl     $0xE0, %eax
    outb    %al, %dx

    movl    $ATA_SECCOUNT, %edx
    movb    $1, %al
    outb    %al, %dx

    movl    $ATA_LBA_LO, %edx
    movl    %ebx, %eax
    outb    %al, %dx

    movl    $ATA_LBA_MID, %edx
    movl    %ebx, %eax
    shrl    $8, %eax
    outb    %al, %dx

    movl    $ATA_LBA_HI, %edx
    movl    %ebx, %eax
    shrl    $16, %eax
    outb    %al, %dx

    movl    $ATA_STATCMD, %edx
    movb    $ATA_CMD_WRITE, %al
    outb    %al, %dx

    movl    $ATA_POLL_MAX, %r13d
.Lwr_wait_drq:
    decl    %r13d
    jz      .Lwr_fail
    movl    $ATA_STATCMD, %edx
    inb     %dx, %al
    testb   $ATA_SR_ERR, %al
    jnz     .Lwr_fail
    testb   $ATA_SR_DF, %al
    jnz     .Lwr_fail
    testb   $ATA_SR_BSY, %al
    jnz     .Lwr_wait_drq
    testb   $ATA_SR_DRQ, %al
    jz      .Lwr_wait_drq

    movl    $ATA_DATA, %edx
    movq    %r12, %rsi
    movl    $256, %ecx
    rep outsw

    movl    $ATA_STATCMD, %edx
    movb    $ATA_CMD_FLUSH, %al
    outb    %al, %dx

    movl    $ATA_POLL_MAX, %r13d
.Lwr_flush:
    decl    %r13d
    jz      .Lwr_fail
    inb     %dx, %al
    testb   $ATA_SR_ERR, %al
    jnz     .Lwr_fail
    testb   $ATA_SR_DF, %al
    jnz     .Lwr_fail
    testb   $ATA_SR_BSY, %al
    jnz     .Lwr_flush

    xorl    %eax, %eax
    pop     %r13
    pop     %r12
    pop     %rbx
    ret

.Lwr_fail:
    movl    $-1, %eax
    pop     %r13
    pop     %r12
    pop     %rbx
    ret
