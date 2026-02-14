#include "vm.h"
#include "vm_mem.h"
#include "vm_cpu.h"
#include "vm_decode.h"
#include "vm_io.h"
#include "vm_loader.h"
#include "vm_display.h"
#include "mem_asm.h"
#include "priority_queue.h"
#include "drivers/drivers.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef VM_ENABLE

#define VM_QUANTUM 64
#define PQ_PRIO_CPU 0
#define PQ_PRIO_DISPLAY 1
#define PQ_PRIO_TIMER 2

static vm_mem_t s_mem;
static vm_cpu_t s_cpu;
static priority_queue_t s_vm_pq;

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
    case VM_OP_RET: {
        uint16_t ip;
        vm_mem_read(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &ip, 2);
        cpu->esp += 2;
        cpu->eip = ip;
        return 0;
    }
    case VM_OP_INT: {
        /* Real mode: push FLAGS, CS, IP; load CS:IP from IVT at vector*4 */
        uint16_t flags = (uint16_t)(cpu->eflags & 0xFFFF);
        uint16_t cs = (uint16_t)cpu->cs;
        uint16_t ip = (uint16_t)cpu->eip;
        cpu->esp -= 2;
        vm_mem_write(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &flags, 2);
        cpu->esp -= 2;
        vm_mem_write(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &cs, 2);
        cpu->esp -= 2;
        vm_mem_write(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &ip, 2);
        uint32_t ivt = (uint32_t)(in->imm & 0xFF) * 4;
        if (ivt + 4 <= mem->size) {
            vm_mem_read(mem, ivt, &ip, 2);
            vm_mem_read(mem, ivt + 2, &cs, 2);
            cpu->eip = ip;
            cpu->cs = cs;
        }
        return 0;
    }
    case VM_OP_IRET: {
        uint16_t ip, cs, flags;
        vm_mem_read(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &ip, 2);
        cpu->esp += 2;
        vm_mem_read(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &cs, 2);
        cpu->esp += 2;
        vm_mem_read(mem, vm_cpu_linear_addr(cpu->ss, cpu->esp), &flags, 2);
        cpu->esp += 2;
        cpu->eip = ip;
        cpu->cs = cs;
        cpu->eflags = (cpu->eflags & 0xFFFF0000) | flags;
        return 0;
    }
    case VM_OP_STOSB: {
        uint32_t addr = vm_cpu_linear_addr(cpu->es, (uint32_t)(cpu->edi & 0xFFFF));
        uint8_t v = get_reg8_lo(cpu, 0);
        vm_mem_write8(mem, addr, v);
        cpu->edi = (cpu->edi & 0xFFFF0000) | ((cpu->edi + 1) & 0xFFFF);
        return 0;
    }
    default:
        return -1;
    }
}

int vm_boot(void) {
    if (vm_mem_init(&s_mem) != 0) return -1;
    vm_cpu_init(&s_cpu);
    vm_io_init();

    /* Guest: write "Flinstone VM" to serial, then HLT. VGA pre-filled below. */
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
    /* Pre-fill VGA buffer at 0xb8000: "Flinstone VM" (char+attr per cell, ASM) */
    {
        static const uint16_t vga_msg[] = {
            0x0746, 0x076c, 0x0769, 0x076e, 0x0773, 0x0774, 0x076f, 0x076e,
            0x0765, 0x0720, 0x0756, 0x074d
        };
        if (GUEST_VGA_BASE + sizeof(vga_msg) <= s_mem.size)
            asm_mem_copy(s_mem.ram + GUEST_VGA_BASE, vga_msg, sizeof(vga_msg));
    }
    s_cpu.eip = 0;
    s_cpu.cs = 0x07c0;
    return 0;
}

/* Execute up to max_instructions. Returns count executed, or 0 on HLT. */
static int vm_run_step(vm_cpu_t *cpu, vm_mem_t *mem, int max_instructions) {
    vm_instr_t instr;
    int count = 0;
    while (count < max_instructions && !cpu->halted) {
        uint32_t linear = guest_eip_linear(cpu);
        if (linear + 16 > mem->size) break;
        if (vm_decode(mem->ram, linear, &instr) != 0) break;
        if (execute(cpu, mem, &instr) != 0) break;
        if (instr.op != VM_OP_JMP)
            cpu->eip += instr.size;
        count++;
    }
    return count;
}

static void vm_cpu_task_fn(void *arg) {
    (void)arg;
    vm_run_step(&s_cpu, &s_mem, VM_QUANTUM);
}

static void vm_display_task_fn(void *arg) {
    (void)arg;
    vm_display_refresh(&s_mem);
}

static void vm_timer_task_fn(void *arg) {
    (void)arg;
    if (g_timer_driver)
        (void)g_timer_driver->tick_count(g_timer_driver);
}

void vm_run(void) {
    vm_cpu_t *cpu = &s_cpu;
    pq_task_t task;

    pq_init(&s_vm_pq);
    pq_push(&s_vm_pq, PQ_PRIO_CPU, vm_cpu_task_fn, NULL);

    while (!cpu->halted) {
        if (pq_pop(&s_vm_pq, &task) != 0)
            break;
        if (task.fn)
            task.fn(task.arg);

        /* Re-schedule after CPU quantum: next display + CPU + timer */
        if (task.fn == vm_cpu_task_fn && !cpu->halted) {
            pq_push(&s_vm_pq, PQ_PRIO_DISPLAY, vm_display_task_fn, NULL);
            pq_push(&s_vm_pq, PQ_PRIO_CPU, vm_cpu_task_fn, NULL);
            pq_push(&s_vm_pq, PQ_PRIO_TIMER, vm_timer_task_fn, NULL);
        }
    }
}

void vm_stop(void) {
    vm_io_shutdown();
    vm_mem_destroy(&s_mem);
}

#endif
