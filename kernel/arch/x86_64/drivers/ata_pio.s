/* ATA PIO 28-bit LBA sector I/O - x86-64 GAS
 *
 * Implements raw 512-byte sector reads and writes via the primary ATA
 * (IDE) channel (I/O ports 0x1F0-0x1F7).  Used by disk_asm.c when
 * DRIVERS_BAREMETAL is defined.
 *
 * Protocol (LBA28, drive 0, polling):
 *   1. Poll BSY (bit 7) in status port until clear.
 *   2. Write drive/head: 0xE0 | LBA[27:24]  (LBA mode, master drive)
 *   3. Write sector count = 1 to 0x1F2.
 *   4. Write LBA[7:0] to 0x1F3, LBA[15:8] to 0x1F4, LBA[23:16] to 0x1F5.
 *   5. Write READ (0x20) or WRITE (0x30) command to 0x1F7.
 *   6. Poll until BSY=0 and DRQ=1.
 *   7. Transfer 256 words (512 bytes) via rep insw / rep outsw.
 *
 * For writes, flush drive write-cache with FLUSH CACHE (0xE7).
 *
 * System V AMD64 ABI:
 *   ata_pio_read_sector (uint32_t lba, void *buf)  - rdi=lba, rsi=buf
 *   ata_pio_write_sector(uint32_t lba, const void *buf) - rdi=lba, rsi=buf
 * Callee-saved registers used: rbx, r12 (saved/restored).
 */

/* ATA primary channel port numbers as GAS constants */
.equ ATA_DATA,     0x1F0
.equ ATA_SECCOUNT, 0x1F2
.equ ATA_LBA_LO,   0x1F3
.equ ATA_LBA_MID,  0x1F4
.equ ATA_LBA_HI,   0x1F5
.equ ATA_DRIVE,    0x1F6
.equ ATA_STATCMD,  0x1F7   /* same port: read=status, write=command */

.equ ATA_CMD_READ,   0x20
.equ ATA_CMD_WRITE,  0x30
.equ ATA_CMD_FLUSH,  0xE7

.equ ATA_SR_BSY,  0x80
.equ ATA_SR_DRQ,  0x08

.section .note.GNU-stack,"",@progbits
.text
.globl ata_pio_read_sector
.globl ata_pio_write_sector

/* ------------------------------------------------------------------ */
/* ata_pio_read_sector(uint32_t lba, void *buf)                        */
/* ------------------------------------------------------------------ */
ata_pio_read_sector:
    push    %rbx
    push    %r12
    movl    %edi, %ebx          /* save lba (lower 32 bits) */
    movq    %rsi, %r12          /* save buf pointer */

    /* 1. Wait until BSY clears */
    movl    $ATA_STATCMD, %edx
.Lrd_wait_bsy:
    inb     %dx, %al
    testb   $ATA_SR_BSY, %al
    jnz     .Lrd_wait_bsy

    /* 2. Drive/head: LBA mode, master drive, LBA[27:24] */
    movl    $ATA_DRIVE, %edx
    movl    %ebx, %eax
    shrl    $24, %eax
    andl    $0x0F, %eax
    orl     $0xE0, %eax
    outb    %al, %dx

    /* 3. Sector count = 1 */
    movl    $ATA_SECCOUNT, %edx
    movb    $1, %al
    outb    %al, %dx

    /* 4. LBA bytes */
    movl    $ATA_LBA_LO, %edx
    movl    %ebx, %eax
    outb    %al, %dx            /* LBA[7:0] */

    movl    $ATA_LBA_MID, %edx
    movl    %ebx, %eax
    shrl    $8, %eax
    outb    %al, %dx            /* LBA[15:8] */

    movl    $ATA_LBA_HI, %edx
    movl    %ebx, %eax
    shrl    $16, %eax
    outb    %al, %dx            /* LBA[23:16] */

    /* 5. Issue READ command */
    movl    $ATA_STATCMD, %edx
    movb    $ATA_CMD_READ, %al
    outb    %al, %dx

    /* 6. Wait for BSY=0 and DRQ=1 */
    movl    $ATA_STATCMD, %edx
.Lrd_wait_drq:
    inb     %dx, %al
    testb   $ATA_SR_BSY, %al
    jnz     .Lrd_wait_drq
    testb   $ATA_SR_DRQ, %al
    jz      .Lrd_wait_drq

    /* 7. Read 256 words (512 bytes) into buf via rep insw */
    movl    $ATA_DATA, %edx
    movq    %r12, %rdi          /* destination = buf */
    movl    $256, %ecx
    rep insw

    pop     %r12
    pop     %rbx
    ret

/* ------------------------------------------------------------------ */
/* ata_pio_write_sector(uint32_t lba, const void *buf)                 */
/* ------------------------------------------------------------------ */
ata_pio_write_sector:
    push    %rbx
    push    %r12
    movl    %edi, %ebx
    movq    %rsi, %r12

    /* 1. Wait BSY */
    movl    $ATA_STATCMD, %edx
.Lwr_wait_bsy:
    inb     %dx, %al
    testb   $ATA_SR_BSY, %al
    jnz     .Lwr_wait_bsy

    /* 2. Drive/head */
    movl    $ATA_DRIVE, %edx
    movl    %ebx, %eax
    shrl    $24, %eax
    andl    $0x0F, %eax
    orl     $0xE0, %eax
    outb    %al, %dx

    /* 3. Sector count = 1 */
    movl    $ATA_SECCOUNT, %edx
    movb    $1, %al
    outb    %al, %dx

    /* 4. LBA bytes */
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

    /* 5. Issue WRITE command */
    movl    $ATA_STATCMD, %edx
    movb    $ATA_CMD_WRITE, %al
    outb    %al, %dx

    /* 6. Wait BSY=0 and DRQ=1 */
    movl    $ATA_STATCMD, %edx
.Lwr_wait_drq:
    inb     %dx, %al
    testb   $ATA_SR_BSY, %al
    jnz     .Lwr_wait_drq
    testb   $ATA_SR_DRQ, %al
    jz      .Lwr_wait_drq

    /* 7. Write 256 words from buf via rep outsw */
    movl    $ATA_DATA, %edx
    movq    %r12, %rsi          /* source = buf */
    movl    $256, %ecx
    rep outsw

    /* Flush write cache */
    movl    $ATA_STATCMD, %edx
    movb    $ATA_CMD_FLUSH, %al
    outb    %al, %dx
.Lwr_flush:
    inb     %dx, %al
    testb   $ATA_SR_BSY, %al
    jnz     .Lwr_flush

    pop     %r12
    pop     %rbx
    ret
