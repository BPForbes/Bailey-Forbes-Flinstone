/* VM snapshot/checkpoint. ASM for all buffer copy. */
#include "vm_snapshot.h"
#include "vm_mem.h"
#include "mem_asm.h"
#include "mem_domain.h"

static uint8_t *s_ram_copy;
static size_t s_ram_size;
static vm_cpu_t s_cpu_copy;
static uint8_t s_kbd_copy[VM_KBD_QUEUE_SIZE];
static size_t s_kbd_head, s_kbd_tail;
static uint64_t s_ticks_copy;
static int s_has_checkpoint;

int vm_snapshot_save(vm_host_t *host) {
    if (!host) return -1;
    vm_mem_t *mem = vm_host_mem(host);
    if (!mem || !mem->ram || mem->size == 0) return -1;

    if (s_ram_copy && s_ram_size != mem->size) {
        mem_domain_free(MEM_DOMAIN_DRIVER, s_ram_copy);
        s_ram_copy = NULL;
    }
    if (!s_ram_copy) {
        s_ram_copy = mem_domain_alloc(MEM_DOMAIN_DRIVER, mem->size);
        if (!s_ram_copy) return -1;
        s_ram_size = mem->size;
    }
    asm_mem_copy(s_ram_copy, mem->ram, mem->size);
    asm_mem_copy(&s_cpu_copy, &host->cpu, sizeof(host->cpu));
    asm_mem_copy(s_kbd_copy, host->kbd_queue, sizeof(host->kbd_queue));
    s_kbd_head = host->kbd_head;
    s_kbd_tail = host->kbd_tail;
    s_ticks_copy = host->vm_ticks;
    s_has_checkpoint = 1;
    return 0;
}

int vm_snapshot_restore(vm_host_t *host) {
    if (!host || !s_has_checkpoint || !s_ram_copy) return -1;
    vm_mem_t *mem = vm_host_mem(host);
    if (!mem || !mem->ram || mem->size != s_ram_size) return -1;

    asm_mem_copy(mem->ram, s_ram_copy, mem->size);
    asm_mem_copy(&host->cpu, &s_cpu_copy, sizeof(host->cpu));
    asm_mem_copy(host->kbd_queue, s_kbd_copy, sizeof(host->kbd_queue));
    host->kbd_head = s_kbd_head;
    host->kbd_tail = s_kbd_tail;
    host->vm_ticks = s_ticks_copy;
    return 0;
}

int vm_snapshot_has_checkpoint(void) {
    return s_has_checkpoint;
}

void vm_snapshot_shutdown(void) {
    if (s_ram_copy) {
        mem_domain_free(MEM_DOMAIN_DRIVER, s_ram_copy);
        s_ram_copy = NULL;
    }
    s_ram_size = 0;
    s_has_checkpoint = 0;
}
