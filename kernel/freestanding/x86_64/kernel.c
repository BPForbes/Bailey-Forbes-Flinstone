#include <stdint.h>
#include <stddef.h>
#define COM1 0x3f8u
struct idt_gate { uint16_t off0, sel; uint8_t ist, flags; uint16_t off1; uint32_t off2, zero; } __attribute__((packed));
struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));
static struct idt_gate idt[256];
static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0,%1" : : "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port)); return value; }
static void serial_putc(char c) { while ((inb(COM1 + 5) & 0x20u) == 0) {} outb(COM1, (uint8_t)c); }
static void serial_puts(const char *s) { while (*s) serial_putc(*s++); }
static void serial_init(void) {
    outb(COM1 + 1, 0); outb(COM1 + 3, 0x80); outb(COM1, 3);
    outb(COM1 + 1, 0); outb(COM1 + 3, 3); outb(COM1 + 2, 0xc7); outb(COM1 + 4, 0x0b);
}
__attribute__((interrupt)) static void early_exception(void *frame) {
    (void)frame;
    serial_puts("FLINTSTONE_EARLY_EXCEPTION\r\n");
    for (;;) __asm__ volatile("cli; hlt");
}
__attribute__((interrupt)) static void early_exception_with_error(void *frame, uintptr_t error) {
    (void)frame; (void)error;
    serial_puts("FLINTSTONE_EARLY_EXCEPTION_WITH_ERROR\r\n");
    for (;;) __asm__ volatile("cli; hlt");
}
static void idt_init(void) {
    for (unsigned i = 0; i < 32; ++i) {
        const uint32_t error_mask = (1u << 8) | (1u << 10) | (1u << 11) |
            (1u << 12) | (1u << 13) | (1u << 14) | (1u << 17) | (1u << 21) |
            (1u << 29) | (1u << 30);
        uintptr_t handler = (error_mask & (1u << i))
            ? (uintptr_t)early_exception_with_error : (uintptr_t)early_exception;
        idt[i].off0 = handler; idt[i].sel = 8; idt[i].ist = 0; idt[i].flags = 0x8e;
        idt[i].off1 = handler >> 16; idt[i].off2 = handler >> 32; idt[i].zero = 0;
    }
    struct idtr descriptor = { (uint16_t)(sizeof(idt) - 1), (uintptr_t)idt };
    __asm__ volatile("lidt %0" : : "m"(descriptor));
}
static void pc_devices_init(void) {
    volatile uint16_t *vga = (volatile uint16_t *)(uintptr_t)0xb8000;
    vga[0] = (uint16_t)(0x0700u | 'F');
    /* Remap and mask the legacy PIC while interrupts remain disabled. */
    outb(0x20, 0x11); outb(0xa0, 0x11); outb(0x21, 0x20); outb(0xa1, 0x28);
    outb(0x21, 4); outb(0xa1, 2); outb(0x21, 1); outb(0xa1, 1);
    outb(0x21, 0xff); outb(0xa1, 0xff);
    /* Program PIT channel 0 to approximately 100 Hz. */
    outb(0x43, 0x36); outb(0x40, 0x9b); outb(0x40, 0x2e);
    /* Prove the emulated 8042 status register is addressable without consuming input. */
    (void)inb(0x64);
}
void fl_kernel_main(void) {
    serial_init();
    idt_init();
    pc_devices_init();
    serial_puts("Flintstone freestanding x86_64\r\n");
    serial_puts("CAP identity=unavailable filesystem=unavailable network=unavailable server=unavailable hosted_sessions=unavailable\r\n");
    serial_puts("FLINTSTONE_KERNEL_BOOT_OK\r\n");
    for (;;) __asm__ volatile("hlt");
}
