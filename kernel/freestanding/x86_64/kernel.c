#include <stdint.h>
#include "identity.h"
#include "keyboard.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"
#include "shell.h"
#include "vga.h"

struct idt_gate {
    uint16_t off0, sel;
    uint8_t ist, flags;
    uint16_t off1;
    uint32_t off2, zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_gate idt[256];

__attribute__((interrupt)) static void early_exception(void *frame)
{
    (void)frame;
    fl_fs_serial_puts("FLINTSTONE_EARLY_EXCEPTION\r\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}

__attribute__((interrupt)) static void early_exception_with_error(void *frame, uintptr_t error)
{
    (void)frame;
    (void)error;
    fl_fs_serial_puts("FLINTSTONE_EARLY_EXCEPTION_WITH_ERROR\r\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}

__attribute__((interrupt)) static void irq_pit(void *frame)
{
    (void)frame;
    fl_fs_pic_eoi(0);
}

__attribute__((interrupt)) static void irq_kbd(void *frame)
{
    (void)frame;
    fl_fs_kbd_irq();
    fl_fs_pic_eoi(1);
}

static void idt_set(unsigned vector, uintptr_t handler)
{
    idt[vector].off0 = (uint16_t)handler;
    idt[vector].sel = 8;
    idt[vector].ist = 0;
    idt[vector].flags = 0x8e;
    idt[vector].off1 = (uint16_t)(handler >> 16);
    idt[vector].off2 = (uint32_t)(handler >> 32);
    idt[vector].zero = 0;
}

static void idt_init(void)
{
    for (unsigned i = 0; i < 32; ++i) {
        const uint32_t error_mask = (1u << 8) | (1u << 10) | (1u << 11) |
            (1u << 12) | (1u << 13) | (1u << 14) | (1u << 17) | (1u << 21) |
            (1u << 29) | (1u << 30);
        uintptr_t handler = (error_mask & (1u << i))
            ? (uintptr_t)early_exception_with_error : (uintptr_t)early_exception;
        idt_set(i, handler);
    }
    idt_set(0x20, (uintptr_t)irq_pit);
    idt_set(0x21, (uintptr_t)irq_kbd);
    struct idtr descriptor = { (uint16_t)(sizeof(idt) - 1), (uintptr_t)idt };
    __asm__ volatile("lidt %0" : : "m"(descriptor));
}

void fl_kernel_main(void)
{
    fl_fs_serial_init();
    idt_init();
    fl_fs_vga_init();
    fl_fs_pic_init();
    fl_fs_pit_init();
    fl_fs_kbd_init();
    fl_fs_identity_init();
    fl_fs_pic_unmask(0);
    fl_fs_pic_unmask(1);
    fl_fs_serial_puts("Flintstone freestanding x86_64\r\n");
    fl_fs_serial_puts("CAP identity=available filesystem=unavailable network=unavailable server=unavailable hosted_sessions=available\r\n");
    fl_fs_serial_puts("FLINTSTONE_KERNEL_BOOT_OK\r\n");
    __asm__ volatile("sti");
    fl_fs_shell_init();
    fl_fs_shell_run();
}
