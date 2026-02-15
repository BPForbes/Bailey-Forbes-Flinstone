#include "vm_cpu.h"
#include "mem_asm.h"

void vm_cpu_init(vm_cpu_t *cpu) {
    if (!cpu) return;
    cpu->eax = cpu->ecx = cpu->edx = cpu->ebx = 0;
    cpu->esp = cpu->ebp = cpu->esi = cpu->edi = 0;
    cpu->eip = 0;
    cpu->cs = 0x07c0;
    cpu->ds = cpu->es = cpu->ss = 0;
    cpu->fs = cpu->gs = 0;
    cpu->eflags = 0;
    cpu->cr0 = cpu->cr3 = 0;
    cpu->halted = 0;
}

/* Real mode: linear = seg*16 + offset */
uint32_t vm_cpu_linear_addr(uint32_t seg, uint32_t offset) {
    return (seg << 4) + offset;
}

/* 32-bit paging: linear[31:22]=PDE, [21:12]=PTE, [11:0]=offset.
 * PDE/PTE: bits [31:12]=base, [0]=P, [1]=R/W, [2]=U/S. */
uint32_t vm_cpu_translate(uint8_t *ram, size_t ram_size, uint32_t cr0, uint32_t cr3, uint32_t linear) {
    if (!(cr0 & VM_CR0_PG)) return linear;
    if (!ram || ram_size < 4096) return linear;
    uint32_t pd_base = cr3 & 0xFFFFF000U;
    uint32_t pde_idx = (linear >> 22) & 0x3FF;
    uint32_t pde_addr = pd_base + pde_idx * 4;
    if (pde_addr + 4 > ram_size) return linear;
    uint32_t pde;
    asm_mem_copy(&pde, ram + pde_addr, 4);
    if (!(pde & 1)) return linear;  /* not present */
    uint32_t pt_base = pde & 0xFFFFF000U;
    uint32_t pte_idx = (linear >> 12) & 0x3FF;
    uint32_t pte_addr = pt_base + pte_idx * 4;
    if (pte_addr + 4 > ram_size) return linear;
    uint32_t pte;
    asm_mem_copy(&pte, ram + pte_addr, 4);
    if (!(pte & 1)) return linear;
    return (pte & 0xFFFFF000U) + (linear & 0xFFF);
}
