/**
 * ARM PL011 UART - real MMIO implementation (QEMU virt @ 0x09000000).
 */
#include "arm_uart.h"
#include "arm_plat.h"
#include "fl/driver/mmio.h"

#define PL011_DR   0x00
#define PL011_FR   0x18
#define PL011_FR_RXFE  (1u << 4)
#define PL011_FR_TXFF  (1u << 5)

static volatile void *uart_base(void) {
    return (volatile void *)ARM_PL011_UART_BASE;
}

int arm_uart_poll(uint8_t *out) {
    if (!out) return -1;
    volatile void *base = uart_base();
    uint32_t fr = fl_mmio_read32((void *)((char *)base + PL011_FR));
    if (fr & PL011_FR_RXFE)
        return -1;
    *out = (uint8_t)fl_mmio_read32((void *)((char *)base + PL011_DR));
    return 0;
}

int arm_uart_getchar(char *out) {
    uint8_t b;
    if (arm_uart_poll(&b) != 0)
        return -1;
    *out = (b < 128) ? (char)b : '\0';
    return 0;
}

void arm_uart_putchar(char c) {
    volatile void *base = uart_base();
    while (fl_mmio_read32((void *)((char *)base + PL011_FR)) & PL011_FR_TXFF)
        ;
    fl_mmio_write32((void *)((char *)base + PL011_DR), (uint32_t)(uint8_t)c);
}
