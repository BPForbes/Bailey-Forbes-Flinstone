/* GPU/VGA: copy guest 0xb8000 to display via asm_mem_copy */
#include "vm_display.h"
#include "vm_mem.h"
#include "mem_asm.h"
#include "mem_domain.h"
#include "drivers/drivers.h"
#include <stdlib.h>

void vm_display_refresh(vm_mem_t *mem) {
    if (!mem || !mem->ram) return;
    if (GUEST_VGA_BASE + GUEST_VGA_SIZE > mem->size) return;
    if (!g_display_driver) return;
    if (!g_display_driver->refresh_vga) return;

    /* Copy guest VGA buffer via ASM (no memcpy) */
    uint16_t *buf = mem_domain_alloc(MEM_DOMAIN_USER, GUEST_VGA_SIZE);
    if (!buf) return;
    asm_mem_copy(buf, mem->ram + GUEST_VGA_BASE, GUEST_VGA_SIZE);
    g_display_driver->refresh_vga(g_display_driver, buf);
    mem_domain_free(MEM_DOMAIN_USER, buf);
}
