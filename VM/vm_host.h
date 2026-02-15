#ifndef VM_HOST_H
#define VM_HOST_H

#include "vm_mem.h"
#include "vm_cpu.h"
#include <stddef.h>
#include <stdint.h>

#define VM_KBD_QUEUE_SIZE 32

typedef struct vm_host {
    vm_mem_t mem;
    vm_cpu_t cpu;
    uint8_t kbd_queue[VM_KBD_QUEUE_SIZE];
    size_t kbd_head;
    size_t kbd_tail;
    uint64_t vm_ticks;   /* deterministic virtual tick (PIT, timer) */
    int running;
} vm_host_t;

int vm_host_create(vm_host_t *host);
void vm_host_destroy(vm_host_t *host);
vm_mem_t *vm_host_mem(vm_host_t *host);
vm_cpu_t *vm_host_cpu(vm_host_t *host);
uint64_t vm_host_ticks(vm_host_t *host);
void vm_host_tick_advance(vm_host_t *host, unsigned int step);
int vm_host_kbd_push(vm_host_t *host, uint8_t scancode);
int vm_host_kbd_pop(vm_host_t *host, uint8_t *out);

#endif /* VM_HOST_H */
