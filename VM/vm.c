#include "vm.h"
#include "vm_mem.h"
#include "vm_cpu.h"
#include "vm_host.h"
#include "vm_decode.h"
#include "vm_io.h"
#include "vm_loader.h"
#include "vm_display.h"
#include "vm_disk.h"
#include "mem_asm.h"
#include "priority_queue.h"
#include "drivers/drivers.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef VM_SDL
#include "vm_sdl.h"
#endif

#ifdef VM_ENABLE

#define VM_QUANTUM 64
#define PQ_PRIO_CPU 0
#define PQ_PRIO_DISPLAY 1
#define PQ_PRIO_TIMER 2

static vm_host_t s_host;
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
    case VM_OP_INC: {
        if (in->dst_reg >= 0 && in->dst_reg < 8) {
            uint32_t v = get_reg32(cpu, in->dst_reg) + 1;
            set_reg32(cpu, in->dst_reg, v);
        }
        return 0;
    }
    case VM_OP_DEC: {
        if (in->dst_reg >= 0 && in->dst_reg < 8) {
            uint32_t v = get_reg32(cpu, in->dst_reg) - 1;
            set_reg32(cpu, in->dst_reg, v);
        }
        return 0;
    }
    case VM_OP_CMP: {
        uint8_t al = get_reg8_lo(cpu, 0);
        uint8_t imm = (uint8_t)(in->imm & 0xFF);
        if (al == imm)
            cpu->eflags |= 0x40;
        else
            cpu->eflags &= ~0x40;
        return 0;
    }
    case VM_OP_JZ:
        if (cpu->eflags & 0x40)
            cpu->eip += (int32_t)(int8_t)in->imm + in->size;
        return 0;
    case VM_OP_JNZ:
        if (!(cpu->eflags & 0x40))
            cpu->eip += (int32_t)(int8_t)in->imm + in->size;
        return 0;
    default:
        return -1;
    }
}

int vm_boot(void) {
    vm_io_init();
    if (vm_host_create(&s_host) != 0) {
        vm_io_shutdown();
        return -1;
    }
    {
        const char *path = getenv("VM_DISK_IMAGE");
        if (!path) path = "vm_disk.img";
        vm_disk_init(path, VM_DISK_DEFAULT_SIZE_MB);
    }
#ifdef VM_SDL
    if (vm_sdl_init() != 0 || vm_sdl_create_window(2) != 0)
        vm_sdl_shutdown();
#endif
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
    vm_run_step(vm_host_cpu(&s_host), vm_host_mem(&s_host), VM_QUANTUM);
}

static void vm_display_task_fn(void *arg) {
    (void)arg;
    vm_display_refresh(vm_host_mem(&s_host));
}

#define VM_TICK_STEP 1
static void vm_timer_task_fn(void *arg) {
    (void)arg;
    vm_host_tick_advance(&s_host, VM_TICK_STEP);
}

void vm_run(void) {
    vm_cpu_t *cpu = vm_host_cpu(&s_host);
    vm_io_set_host(&s_host);

#ifdef VM_SDL
    if (vm_sdl_is_active()) {
        while (!cpu->halted && !vm_sdl_is_quit()) {
            vm_sdl_poll_events(&s_host);
            if (!vm_host_is_paused(&s_host))
                vm_cpu_task_fn(NULL);
            vm_host_tick_advance(&s_host, VM_TICK_STEP);
            if (!cpu->halted)
                vm_sdl_present(&s_host);
        }
    } else
#endif
    {
        pq_task_t task;
        pq_init(&s_vm_pq);
        pq_push(&s_vm_pq, PQ_PRIO_CPU, vm_cpu_task_fn, NULL);
        while (!cpu->halted) {
            if (pq_pop(&s_vm_pq, &task) != 0)
                break;
            if (task.fn)
                task.fn(task.arg);
            if (task.fn == vm_cpu_task_fn && !cpu->halted) {
                pq_push(&s_vm_pq, PQ_PRIO_DISPLAY, vm_display_task_fn, NULL);
                pq_push(&s_vm_pq, PQ_PRIO_CPU, vm_cpu_task_fn, NULL);
                pq_push(&s_vm_pq, PQ_PRIO_TIMER, vm_timer_task_fn, NULL);
            }
        }
    }

    vm_io_set_host(NULL);
}

void vm_stop(void) {
    vm_io_set_host(NULL);
    vm_disk_shutdown();
#ifdef VM_SDL
    vm_sdl_shutdown();
#endif
    vm_host_destroy(&s_host);
    vm_io_shutdown();
}

void vm_step_one(void) {
    vm_run_step(vm_host_cpu(&s_host), vm_host_mem(&s_host), 1);
}

#endif
