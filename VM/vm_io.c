#include "vm_io.h"
#include "vm_mem.h"
#include "vm_cpu.h"
#include "vm_host.h"
#include "vm_disk.h"
#include "mem_asm.h"
#include "mem_domain.h"
#include "drivers/drivers.h"
#include "drivers/driver_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static FILE *s_serial_out;

#define SECTOR_SIZE 512

static uint8_t s_sector_buf[SECTOR_SIZE];
static uint32_t s_ide_lba;
static int s_ide_byte_idx;
static uint8_t s_pit_mode;
static vm_host_t *s_host;

void vm_io_init(void) {
    s_host = NULL;
    asm_mem_zero(s_sector_buf, SECTOR_SIZE);
    s_ide_lba = 0;
    s_ide_byte_idx = SECTOR_SIZE;
    s_serial_out = stdout;
}

void vm_io_shutdown(void) {
    s_host = NULL;
}

void vm_io_set_host(vm_host_t *host) {
    s_host = host;
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
    if (port == 0x40 && g_timer_driver)
        return (uint8_t)(g_timer_driver->tick_count(g_timer_driver) & 0xFF);
    if (port == 0x43) return s_pit_mode;
    return 0;
}

static uint8_t vm_io_in_pic(uint32_t port) {
    if (port == 0x20 || port == 0xA0) return 0;
    if (port == 0x21 || port == 0xA1) return 0xFF;
    return 0xFF;
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
    if (size == 1) return v & 0xFF;
    return v & 0xFFFF;
}

void vm_io_out(vm_mem_t *mem, uint32_t port, uint32_t value, int size) {
    (void)mem;
    (void)size;
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
