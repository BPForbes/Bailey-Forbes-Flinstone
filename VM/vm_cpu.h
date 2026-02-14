#ifndef VM_CPU_H
#define VM_CPU_H

#include <stdint.h>

#define VM_REG_EAX 0
#define VM_REG_ECX 1
#define VM_REG_EDX 2
#define VM_REG_EBX 3
#define VM_REG_ESP 4
#define VM_REG_EBP 5
#define VM_REG_ESI 6
#define VM_REG_EDI 7

typedef struct vm_cpu {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t eip;
    uint32_t cs, ds, es, ss, fs, gs;
    uint32_t eflags;
    int halted;
} vm_cpu_t;

void vm_cpu_init(vm_cpu_t *cpu);
uint32_t vm_cpu_linear_addr(uint32_t seg, uint32_t offset);

#endif /* VM_CPU_H */
