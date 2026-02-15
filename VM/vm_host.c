/* Host owns VM data: guest RAM, vCPU, keyboard queue. Parent system maintains via host. */
#include "vm_host.h"
#include "vm_mem.h"
#include "vm_cpu.h"
#include "vm_loader.h"
#include "mem_asm.h"
#include "drivers/driver_types.h"
#include <stdlib.h>

static const uint8_t s_minimal_guest[] = {
    0xB0, 'F', 0xE6, 0xF8, 0xB0, 'l', 0xE6, 0xF8, 0xB0, 'i', 0xE6, 0xF8,
    0xB0, 'n', 0xE6, 0xF8, 0xB0, 's', 0xE6, 0xF8, 0xB0, 't', 0xE6, 0xF8,
    0xB0, 'o', 0xE6, 0xF8, 0xB0, 'n', 0xE6, 0xF8, 0xB0, 'e', 0xE6, 0xF8,
    0xB0, ' ', 0xE6, 0xF8, 0xB0, 'V', 0xE6, 0xF8, 0xB0, 'M', 0xE6, 0xF8,
    0xB0, '\n', 0xE6, 0xF8,
    0xF4
};

static const uint16_t s_vga_msg[] = {
    0x0746, 0x076c, 0x0769, 0x076e, 0x0773, 0x0774, 0x076f, 0x076e,
    0x0765, 0x0720, 0x0756, 0x074d
};

int vm_host_create(vm_host_t *host) {
    if (!host) return -1;
    asm_mem_zero(host, sizeof(*host));
    if (vm_mem_init(&host->mem) != 0) return -1;
    vm_cpu_init(&host->cpu);
    host->running = 1;
    if (vm_load_binary(&host->mem, GUEST_LOAD_ADDR, s_minimal_guest, sizeof(s_minimal_guest)) != 0) {
        vm_mem_destroy(&host->mem);
        return -1;
    }
    if (GUEST_VGA_BASE + sizeof(s_vga_msg) <= host->mem.size)
        asm_mem_copy(host->mem.ram + GUEST_VGA_BASE, s_vga_msg, sizeof(s_vga_msg));
    host->cpu.eip = 0;
    host->cpu.cs = 0x07c0;
    return 0;
}

void vm_host_destroy(vm_host_t *host) {
    if (!host) return;
    vm_mem_destroy(&host->mem);
    asm_mem_zero(host, sizeof(*host));
}

vm_mem_t *vm_host_mem(vm_host_t *host) {
    return host ? &host->mem : NULL;
}

vm_cpu_t *vm_host_cpu(vm_host_t *host) {
    return host ? &host->cpu : NULL;
}

uint64_t vm_host_ticks(vm_host_t *host) {
    return host ? host->vm_ticks : 0;
}

void vm_host_tick_advance(vm_host_t *host, unsigned int step) {
    if (host)
        host->vm_ticks += step;
}

int vm_host_kbd_push(vm_host_t *host, uint8_t scancode) {
    if (!host) return -1;
    size_t next = (host->kbd_tail + 1) % VM_KBD_QUEUE_SIZE;
    if (next == host->kbd_head) return -1;
    host->kbd_queue[host->kbd_tail] = scancode;
    host->kbd_tail = next;
    return 0;
}

int vm_host_kbd_pop(vm_host_t *host, uint8_t *out) {
    if (!host || !out) return -1;
    if (host->kbd_head == host->kbd_tail) return -1;
    *out = host->kbd_queue[host->kbd_head];
    host->kbd_head = (host->kbd_head + 1) % VM_KBD_QUEUE_SIZE;
    return 0;
}

void vm_host_pause(vm_host_t *host) {
    if (host) host->paused = 1;
}

void vm_host_resume(vm_host_t *host) {
    if (host) host->paused = 0;
}

int vm_host_is_paused(vm_host_t *host) {
    return host ? host->paused : 0;
}

void vm_host_reset(vm_host_t *host) {
    if (!host) return;
    vm_mem_zero(&host->mem);
    vm_load_binary(&host->mem, GUEST_LOAD_ADDR, s_minimal_guest, sizeof(s_minimal_guest));
    if (GUEST_VGA_BASE + sizeof(s_vga_msg) <= host->mem.size)
        asm_mem_copy(host->mem.ram + GUEST_VGA_BASE, s_vga_msg, sizeof(s_vga_msg));
    vm_cpu_init(&host->cpu);
    host->cpu.eip = 0;
    host->cpu.cs = 0x07c0;
    host->cpu.halted = 0;
    host->paused = 0;
}

void vm_host_dump_registers(const vm_host_t *host, FILE *out) {
    if (!host || !out) return;
    const vm_cpu_t *c = &host->cpu;
    fprintf(out, "EAX=%08X ECX=%08X EDX=%08X EBX=%08X\n", c->eax, c->ecx, c->edx, c->ebx);
    fprintf(out, "ESP=%08X EBP=%08X ESI=%08X EDI=%08X\n", c->esp, c->ebp, c->esi, c->edi);
    fprintf(out, "EIP=%08X CS=%04X DS=%04X SS=%04X ES=%04X\n", c->eip, c->cs & 0xFFFF, c->ds & 0xFFFF, c->ss & 0xFFFF, c->es & 0xFFFF);
    fprintf(out, "CR0=%08X CR3=%08X EFLAGS=%08X %s\n", c->cr0, c->cr3, c->eflags, c->halted ? "[HALTED]" : "");
}
