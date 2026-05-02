#include "vm_io.h"
#include "vm_mem.h"
#include "vm_cpu.h"
#include "vm_host.h"
#include "vm_disk.h"
#include "fl/syscall.h"
#include "mem_asm.h"
#include "mem_domain.h"
#include "drivers/drivers.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

/* SECTOR_SIZE from driver_types.h */
#define PCI_CFG_ADDR  0xCF8
#define PCI_CFG_DATA  0xCFC
#define PCI_CFG_RESET 0xCF9
#define VM_PCI_DEV_MAX 4
#define PCI_CFG_SIZE  256

static uint8_t s_sector_buf[SECTOR_SIZE];
static FILE *s_serial_out;
static uint32_t s_ide_lba;
static int s_ide_byte_idx;
static uint8_t s_pit_mode;
static vm_host_t *s_host;
static uint32_t s_pci_addr;
static int s_reset_requested;
/* Virtual PCI config: bus 0, dev 0..3. ASM-backed via mem_domain. */
static uint8_t *s_pci_cfg;
static uint64_t s_sys_no;
static uint64_t s_sys_args[4];
static int64_t s_sys_ret;
static int s_io_inited;

#define VM_SYS_PORT_NO      0xE0
#define VM_SYS_PORT_ARG0    0xE1
#define VM_SYS_PORT_ARG1    0xE2
#define VM_SYS_PORT_ARG2    0xE3
#define VM_SYS_PORT_ARG3    0xE4
#define VM_SYS_PORT_CALL    0xE5
#define VM_SYS_PORT_RET     0xE6
#define VM_SYS_PORT_ARG0_HI 0xE7
#define VM_SYS_PORT_ARG1_HI 0xE8
#define VM_SYS_PORT_ARG2_HI 0xE9
#define VM_SYS_PORT_ARG3_HI 0xEA
#define VM_SYS_PORT_RET_HI  0xEB

static uint32_t low32(uint64_t value) {
    return (uint32_t)(value & 0xFFFFFFFFu);
}

static uint32_t high32(uint64_t value) {
    return (uint32_t)(value >> 32);
}

static uint32_t read_port_width(uint32_t value, int size) {
    if (size == 1) return value & 0xFFu;
    if (size == 2) return value & 0xFFFFu;
    return value;
}

static uint32_t merge_port_width(uint32_t old_value, uint32_t value, int size) {
    uint32_t mask = (size == 1) ? 0xFFu : (size == 2) ? 0xFFFFu : 0xFFFFFFFFu;
    return (old_value & ~mask) | (value & mask);
}

/* Syscall bridge uses explicit 64-bit slots (guest may expose args as low/high port pairs). */
static void write_port_width(uint64_t *slot, uint32_t value, int size) {
    uint32_t low = merge_port_width(low32(*slot), value, size);
    *slot = ((uint64_t)high32(*slot) << 32) | (uint64_t)low;
}

static void write_port_high_width(uint64_t *slot, uint32_t value, int size) {
    uint64_t full = *slot;
    uint32_t hi = merge_port_width(high32(full), value, size);
    uint64_t low = (uint64_t)low32(full);
    *slot = low | ((uint64_t)hi << 32);
}

/* Translate guest offset to host pointer; returns 0 if invalid or out of range. */
static uintptr_t vm_sys_arg_to_host_ptr(vm_mem_t *mem, uintptr_t arg, size_t len) {
    if (!mem || !mem->ram || len == 0) {
        return 0;
    }
    if (arg >= mem->size) {
        return 0;
    }
    if (len > mem->size - (size_t)arg) {
        return 0;
    }
    return (uintptr_t)(mem->ram + (size_t)arg);
}

static void vm_translate_sys_args(vm_mem_t *mem, uintptr_t args[4]) {
    switch ((fl_syscall_no_t)s_sys_no) {
        case FL_SYS_WRITE:
        case FL_SYS_READ:
            args[0] = vm_sys_arg_to_host_ptr(mem, args[0], (size_t)args[1]);
            break;
        case FL_SYS_PIPE_READ:
        case FL_SYS_PIPE_WRITE:
        case FL_SYS_MSGQ_SEND:
        case FL_SYS_MSGQ_RECV:
            args[1] = vm_sys_arg_to_host_ptr(mem, args[1], (size_t)args[2]);
            break;
        default:
            break;
    }
}

static void vm_pci_init_cfg(void) {
    if (s_pci_cfg)
        mem_domain_free(MEM_DOMAIN_DRIVER, s_pci_cfg);
    s_pci_cfg = mem_domain_alloc(MEM_DOMAIN_DRIVER, VM_PCI_DEV_MAX * PCI_CFG_SIZE);
    if (!s_pci_cfg) return;
    asm_mem_zero(s_pci_cfg, VM_PCI_DEV_MAX * PCI_CFG_SIZE);
    /* Dev 0: Host bridge - 0x1234:0x0001, class 0600 */
    s_pci_cfg[0*PCI_CFG_SIZE + 0] = 0x34; s_pci_cfg[0*PCI_CFG_SIZE + 1] = 0x12;
    s_pci_cfg[0*PCI_CFG_SIZE + 2] = 0x01; s_pci_cfg[0*PCI_CFG_SIZE + 3] = 0x00;
    s_pci_cfg[0*PCI_CFG_SIZE + 9] = 0x00; s_pci_cfg[0*PCI_CFG_SIZE + 10] = 0x06; s_pci_cfg[0*PCI_CFG_SIZE + 11] = 0x00;
    /* Dev 1: IDE - 0x1234:0x1111, class 0101 */
    s_pci_cfg[1*PCI_CFG_SIZE + 0] = 0x34; s_pci_cfg[1*PCI_CFG_SIZE + 1] = 0x12;
    s_pci_cfg[1*PCI_CFG_SIZE + 2] = 0x11; s_pci_cfg[1*PCI_CFG_SIZE + 3] = 0x11;
    s_pci_cfg[1*PCI_CFG_SIZE + 9] = 0x01; s_pci_cfg[1*PCI_CFG_SIZE + 10] = 0x01; s_pci_cfg[1*PCI_CFG_SIZE + 11] = 0x00;
    /* Dev 2: VGA - 0x1234:0x2222, class 0300 */
    s_pci_cfg[2*PCI_CFG_SIZE + 0] = 0x34; s_pci_cfg[2*PCI_CFG_SIZE + 1] = 0x12;
    s_pci_cfg[2*PCI_CFG_SIZE + 2] = 0x22; s_pci_cfg[2*PCI_CFG_SIZE + 3] = 0x22;
    s_pci_cfg[2*PCI_CFG_SIZE + 9] = 0x00; s_pci_cfg[2*PCI_CFG_SIZE + 10] = 0x03; s_pci_cfg[2*PCI_CFG_SIZE + 11] = 0x00;
    /* Dev 3: KBD - 0x1234:0x3333, class 0C03 */
    s_pci_cfg[3*PCI_CFG_SIZE + 0] = 0x34; s_pci_cfg[3*PCI_CFG_SIZE + 1] = 0x12;
    s_pci_cfg[3*PCI_CFG_SIZE + 2] = 0x33; s_pci_cfg[3*PCI_CFG_SIZE + 3] = 0x33;
    s_pci_cfg[3*PCI_CFG_SIZE + 9] = 0x03; s_pci_cfg[3*PCI_CFG_SIZE + 10] = 0x0C; s_pci_cfg[3*PCI_CFG_SIZE + 11] = 0x00;
}

void vm_io_init(void) {
    s_host = NULL;
    s_reset_requested = 0;
    s_pci_addr = 0;
    asm_mem_zero(s_sector_buf, SECTOR_SIZE);
    vm_pci_init_cfg();
    s_ide_lba = 0;
    s_ide_byte_idx = SECTOR_SIZE;
    s_serial_out = stdout;
    s_sys_no = 0;
    asm_mem_zero(s_sys_args, sizeof(s_sys_args));
    s_sys_ret = 0;
    fl_sys_bootstrap();
    s_io_inited = 1;
}

int vm_io_reset_requested(void) {
    return s_reset_requested;
}
void vm_io_clear_reset(void) {
    s_reset_requested = 0;
}

void vm_io_shutdown(void) {
    s_host = NULL;
    if (s_pci_cfg) {
        mem_domain_free(MEM_DOMAIN_DRIVER, s_pci_cfg);
        s_pci_cfg = NULL;
    }
    fl_sys_shutdown();
    s_io_inited = 0;
}

void vm_io_set_host(vm_host_t *host) {
    s_host = host;
}

int vm_io_pci_ready(void) {
    return s_pci_cfg != NULL;
}

int vm_io_serial_ready(void) {
    return s_serial_out != NULL;
}

int vm_io_syscall_bridge_ready(void) {
    return s_io_inited;
}

int vm_io_host_bound(void) {
    return s_host != NULL;
}

int vm_io_reset_line_ready(void) {
    return s_io_inited;
}

static uint8_t vm_io_in_ide(uint32_t port) {
    if (port == 0x1f0) {
        if (s_ide_byte_idx >= SECTOR_SIZE) {
            if (vm_disk_is_active()) {
                if (vm_disk_read_sector(s_ide_lba, s_sector_buf) != 0)
                    asm_mem_zero(s_sector_buf, SECTOR_SIZE);
            } else if (g_block_driver && g_block_driver->read_sector) {
                if (g_block_driver->read_sector(g_block_driver, s_ide_lba, s_sector_buf) != 0)
                    asm_mem_zero(s_sector_buf, SECTOR_SIZE);
            }
            s_ide_byte_idx = 0;
        }
        uint8_t v = (s_ide_byte_idx < SECTOR_SIZE) ? s_sector_buf[s_ide_byte_idx] : 0;
        s_ide_byte_idx++;
        return v;
    }
    if (port == 0x1f1) return 0;
    if (port == 0x1f2) return 1;
    if (port == 0x1f3) return (uint8_t)(s_ide_lba);
    if (port == 0x1f4) return (uint8_t)(s_ide_lba >> 8);
    if (port == 0x1f5) return (uint8_t)(s_ide_lba >> 16);
    if (port == 0x1f7) return 0x40;
    return 0xFF;
}

static uint8_t vm_io_in_keyboard(uint32_t port) {
    if (port == 0x60) {
        if (s_host) {
            uint8_t sc;
            if (vm_host_kbd_pop(s_host, &sc) == 0)
                return sc;
        }
        if (g_keyboard_driver && g_keyboard_driver->poll_scancode) {
            uint8_t sc;
            if (g_keyboard_driver->poll_scancode(g_keyboard_driver, &sc) == 0)
                return sc;
        }
    }
    return 0;
}

static uint8_t vm_io_in_pit(uint32_t port) {
    if (port == 0x40) {
        if (s_host)
            return (uint8_t)(vm_host_ticks(s_host) & 0xFF);
        if (g_timer_driver)
            return (uint8_t)(g_timer_driver->tick_count(g_timer_driver) & 0xFF);
    }
    if (port == 0x43) return s_pit_mode;
    return 0;
}

static uint8_t vm_io_in_pic(uint32_t port) {
    if (port == 0x20 || port == 0xA0) return 0;
    if (port == 0x21 || port == 0xA1) return 0xFF;
    return 0xFF;
}

static uint32_t vm_io_in_pci(uint32_t port) {
    if (port != PCI_CFG_DATA) return 0xFFFFFFFFu;
    if (!(s_pci_addr & 0x80000000u)) return 0xFFFFFFFFu;
    uint8_t bus = (s_pci_addr >> 16) & 0xFF;
    uint8_t dev = (s_pci_addr >> 11) & 0x1F;
    uint8_t reg = (s_pci_addr >>  2) & 0x3F;
    if (bus != 0 || dev >= VM_PCI_DEV_MAX || !s_pci_cfg) return 0xFFFFFFFFu;
    uint32_t v = 0;
    if (reg * 4 + 4 <= PCI_CFG_SIZE) {
        const uint8_t *cfg = s_pci_cfg + dev * PCI_CFG_SIZE;
        v = (uint32_t)cfg[reg*4] | ((uint32_t)cfg[reg*4+1]<<8)
          | ((uint32_t)cfg[reg*4+2]<<16) | ((uint32_t)cfg[reg*4+3]<<24);
    }
    return v;
}

uint32_t vm_io_in(vm_mem_t *mem, uint32_t port, int size) {
    (void)mem;
    uint32_t v = 0xFF;
    if (port >= 0x1f0 && port <= 0x1f7)
        v = vm_io_in_ide(port);
    else if (port == 0x60 || port == 0x64)
        v = vm_io_in_keyboard(port);
    else if (port >= 0x40 && port <= 0x43)
        v = vm_io_in_pit(port);
    else if (port == 0x20 || port == 0x21 || port == 0xA0 || port == 0xA1)
        v = vm_io_in_pic(port);
    else if (port == PCI_CFG_DATA && size == 4)
        return vm_io_in_pci(port);
    else if (port == VM_SYS_PORT_RET)
        return read_port_width(low32((uint64_t)s_sys_ret), size);
    else if (port == VM_SYS_PORT_RET_HI)
        return read_port_width(high32((uint64_t)s_sys_ret), size);
    if (size == 1) return v & 0xFF;
    if (size == 2) return v & 0xFFFF;
    return v & 0xFFFFFFFFu;
}

void vm_io_out(vm_mem_t *mem, uint32_t port, uint32_t value, int size) {
    if (port == PCI_CFG_ADDR) {
        s_pci_addr = value;
        return;
    }
    if (port >= VM_SYS_PORT_NO && port <= VM_SYS_PORT_ARG3) {
        if (port == VM_SYS_PORT_NO) write_port_width(&s_sys_no, value, size);
        else write_port_width(&s_sys_args[port - VM_SYS_PORT_ARG0], value, size);
        return;
    }
    if (port >= VM_SYS_PORT_ARG0_HI && port <= VM_SYS_PORT_ARG3_HI) {
        write_port_high_width(&s_sys_args[port - VM_SYS_PORT_ARG0_HI], value, size);
        return;
    }
    if (port == VM_SYS_PORT_CALL) {
        uintptr_t args[4] = {
            s_sys_args[0], s_sys_args[1], s_sys_args[2], s_sys_args[3]
        };
        vm_translate_sys_args(mem, args);
        long ret = fl_syscall_dispatch((fl_syscall_no_t)s_sys_no,
                                       args[0], args[1], args[2], args[3]);
        s_sys_ret = ret;
        return;
    }
    if (port == PCI_CFG_DATA && (s_pci_addr & 0x80000000u)) {
        uint8_t bus = (s_pci_addr >> 16) & 0xFF;
        uint8_t dev = (s_pci_addr >> 11) & 0x1F;
        uint8_t reg = (s_pci_addr >>  2) & 0x3F;
        if (bus == 0 && dev < VM_PCI_DEV_MAX && reg * 4 + 4 <= PCI_CFG_SIZE && s_pci_cfg) {
            uint8_t *cfg = s_pci_cfg + dev * PCI_CFG_SIZE;
            cfg[reg*4]   = (uint8_t)(value);
            cfg[reg*4+1] = (uint8_t)(value >> 8);
            cfg[reg*4+2] = (uint8_t)(value >> 16);
            cfg[reg*4+3] = (uint8_t)(value >> 24);
        }
        return;
    }
    if (port == PCI_CFG_RESET) {
        if ((value & 0x0E) == 0x06 || (value & 0x0E) == 0x0E)
            s_reset_requested = 1;
        return;
    }
    if (port >= 0x1f0 && port <= 0x1f7) {
        if (port == 0x1f3) s_ide_lba = (s_ide_lba & 0xFFFFFF00) | (value & 0xFF);
        else if (port == 0x1f4) s_ide_lba = (s_ide_lba & 0xFFFF00FF) | ((value & 0xFF) << 8);
        else if (port == 0x1f5) s_ide_lba = (s_ide_lba & 0xFF00FFFF) | ((value & 0xFF) << 16);
        else if (port == 0x1f0) {
            if (s_ide_byte_idx >= SECTOR_SIZE)
                s_ide_byte_idx = 0;
            s_sector_buf[s_ide_byte_idx++] = (uint8_t)(value & 0xFF);
            if (s_ide_byte_idx >= SECTOR_SIZE) {
                if (vm_disk_is_active())
                    vm_disk_write_sector(s_ide_lba, s_sector_buf);
                else if (g_block_driver && g_block_driver->write_sector)
                    g_block_driver->write_sector(g_block_driver, s_ide_lba, s_sector_buf);
                asm_mem_zero(s_sector_buf, SECTOR_SIZE);
            }
        }
    } else if (port == 0x3f8 || port == 0xf8) {
        if (s_serial_out) {
            fputc((char)(value & 0xFF), s_serial_out);
            fflush(s_serial_out);
        }
    } else if (port >= 0x40 && port <= 0x43) {
        if (port == 0x43) s_pit_mode = (uint8_t)(value & 0xFF);
        /* Port 0x40: gate/counter - ignore for now, timer_driver provides ticks */
    } else if (port == 0x20 || port == 0xA0) {
        /* PIC EOI - acknowledge interrupt */
        if (g_pic_driver && g_pic_driver->eoi)
            g_pic_driver->eoi(g_pic_driver, port == 0xA0 ? 8 : 0);
    } else if (port == 0x21 || port == 0xA1) {
        /* PIC mask - ignore for host */
    }
}
