#include "vm_cpu.h"

void vm_cpu_init(vm_cpu_t *cpu) {
    if (!cpu) return;
    cpu->eax = cpu->ecx = cpu->edx = cpu->ebx = 0;
    cpu->esp = cpu->ebp = cpu->esi = cpu->edi = 0;
    cpu->eip = 0;
    cpu->cs = 0x07c0;
    cpu->ds = cpu->es = cpu->ss = 0;
    cpu->fs = cpu->gs = 0;
    cpu->eflags = 0;
    cpu->halted = 0;
}

/* Real mode: linear = seg*16 + offset */
uint32_t vm_cpu_linear_addr(uint32_t seg, uint32_t offset) {
    return (seg << 4) + offset;
}
