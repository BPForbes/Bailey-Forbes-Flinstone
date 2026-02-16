#ifndef VM_CPU_H
#define VM_CPU_H

#include <stddef.h>
#include <stdint.h>

#define VM_REG_EAX 0
#define VM_REG_ECX 1
#define VM_REG_EDX 2
#define VM_REG_EBX 3
#define VM_REG_ESP 4
#define VM_REG_EBP 5
#define VM_REG_ESI 6
#define VM_REG_EDI 7

#define VM_CR0_PE  0x00000001U  /* Protected mode enable */
#define VM_CR0_PG  0x80000000U  /* Paging enable */

typedef struct vm_cpu {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t eip;
    uint32_t cs, ds, es, ss, fs, gs;
    uint32_t eflags;
    uint32_t cr0, cr3;   /* CR0.PE, CR0.PG; CR3 = page directory base */
    int halted;
} vm_cpu_t;

void vm_cpu_init(vm_cpu_t *cpu);
uint32_t vm_cpu_linear_addr(uint32_t seg, uint32_t offset);
/* Translate linear to physical when paging enabled. Returns linear if PG=0 or invalid. */
uint32_t vm_cpu_translate(uint8_t *ram, size_t ram_size, uint32_t cr0, uint32_t cr3, uint32_t linear);

#endif /* VM_CPU_H */
