#include "vm.h"
#include "vm_mem.h"
#include "vm_cpu.h"
#include "vm_decode.h"
#include "vm_io.h"
#include "vm_loader.h"
#include "mem_asm.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef VM_ENABLE

static vm_mem_t s_mem;
static vm_cpu_t s_cpu;

static uint32_t get_reg32(vm_cpu_t *cpu, int r) {
    uint32_t *regs[] = { &cpu->eax, &cpu->ecx, &cpu->edx, &cpu->ebx,
                         &cpu->esp, &cpu->ebp, &cpu->esi, &cpu->edi };
    return (r >= 0 && r < 8) ? *regs[r] : 0;
}

static void set_reg32(vm_cpu_t *cpu, int r, uint32_t v) {
    uint32_t *regs[] = { &cpu->eax, &cpu->ecx, &cpu->edx, &cpu->ebx,
                         &cpu->esp, &cpu->ebp, &cpu->esi, &cpu->edi };
    if (r >= 0 && r < 8) *regs[r] = v;
}

static uint8_t get_reg8_lo(vm_cpu_t *cpu, int r) {
    return (uint8_t)(get_reg32(cpu, r) & 0xFF);
}

static void set_reg8_lo(vm_cpu_t *cpu, int r, uint8_t v) {
    uint32_t x = get_reg32(cpu, r);
    set_reg32(cpu, r, (x & 0xFFFFFF00) | v);
}

static uint32_t guest_eip_linear(vm_cpu_t *cpu) {
    return vm_cpu_linear_addr(cpu->cs, cpu->eip);
}

static int execute(vm_cpu_t *cpu, vm_mem_t *mem, vm_instr_t *in) {
    switch (in->op) {
    case VM_OP_NOP:
        return 0;
    case VM_OP_HLT:
        cpu->halted = 1;
        return 0;
    case VM_OP_IN:
        set_reg8_lo(cpu, 0, (uint8_t)vm_io_in(mem, (uint32_t)in->imm, 1));
        return 0;
    case VM_OP_OUT:
        vm_io_out(mem, (uint32_t)in->imm, get_reg8_lo(cpu, 0), 1);
        return 0;
    case VM_OP_MOV:
        if (in->dst_reg >= 0 && in->src_reg < 0)
            set_reg32(cpu, in->dst_reg, in->imm);
        return 0;
    case VM_OP_PUSH: {
        cpu->esp -= 4;
        uint32_t v = get_reg32(cpu, in->dst_reg);
        vm_mem_write(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &v, 4);
        return 0;
    }
    case VM_OP_POP: {
        uint32_t v;
        vm_mem_read(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &v, 4);
        cpu->esp += 4;
        set_reg32(cpu, in->dst_reg, v);
        return 0;
    }
    case VM_OP_JMP:
        cpu->eip += (int32_t)(int8_t)in->imm + in->size;
        return 0;
    default:
        return -1;
    }
}

int vm_boot(void) {
    if (vm_mem_init(&s_mem) != 0) return -1;
    vm_cpu_init(&s_cpu);
    vm_io_init();

    /* Minimal guest: write "Flinstone VM" to serial (port 0xF8), then HLT */
    static const uint8_t minimal_guest[] = {
        0xB0, 'F', 0xE6, 0xF8, 0xB0, 'l', 0xE6, 0xF8, 0xB0, 'i', 0xE6, 0xF8,
        0xB0, 'n', 0xE6, 0xF8, 0xB0, 's', 0xE6, 0xF8, 0xB0, 't', 0xE6, 0xF8,
        0xB0, 'o', 0xE6, 0xF8, 0xB0, 'n', 0xE6, 0xF8, 0xB0, 'e', 0xE6, 0xF8,
        0xB0, ' ', 0xE6, 0xF8, 0xB0, 'V', 0xE6, 0xF8, 0xB0, 'M', 0xE6, 0xF8,
        0xB0, '\n', 0xE6, 0xF8,
        0xF4
    };
    if (vm_load_binary(&s_mem, GUEST_LOAD_ADDR, minimal_guest, sizeof(minimal_guest)) != 0) {
        vm_mem_destroy(&s_mem);
        return -1;
    }
    s_cpu.eip = 0;
    s_cpu.cs = 0x07c0;
    return 0;
}

void vm_run(void) {
    vm_cpu_t *cpu = &s_cpu;
    vm_mem_t *mem = &s_mem;
    vm_instr_t instr;

    while (!cpu->halted) {
        uint32_t linear = guest_eip_linear(cpu);
        if (linear + 16 > mem->size) break;
        if (vm_decode(mem->ram, linear, &instr) != 0) break;
        if (execute(cpu, mem, &instr) != 0) break;
        if (instr.op != VM_OP_JMP)
            cpu->eip += instr.size;
    }
}

void vm_stop(void) {
    vm_io_shutdown();
    vm_mem_destroy(&s_mem);
}

#endif
