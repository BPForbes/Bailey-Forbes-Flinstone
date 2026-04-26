#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "VM/devices/vm_arch.h"
#include "VM/devices/vm_host.h"
#include "VM/devices/vm_io.h"
#include "drivers.h"

/* Driver globals used by vm_arch_collect. */
static block_driver_t s_block_driver;
static keyboard_driver_t s_keyboard_driver;
static display_driver_t s_display_driver;
static timer_driver_t s_timer_driver;
static pic_driver_t s_pic_driver;

block_driver_t    *g_block_driver = &s_block_driver;
keyboard_driver_t *g_keyboard_driver = &s_keyboard_driver;
display_driver_t  *g_display_driver = &s_display_driver;
timer_driver_t    *g_timer_driver = &s_timer_driver;
pic_driver_t      *g_pic_driver = &s_pic_driver;

/* Minimal shims for vm_io/vm_arch dependencies. */
int __attribute__((weak)) vm_host_kbd_pop(vm_host_t *host, uint8_t *out) { (void)host; (void)out; return -1; }
uint64_t __attribute__((weak)) vm_host_ticks(vm_host_t *host) { (void)host; return 0; }
int __attribute__((weak)) vm_disk_is_active(void) { return 1; }
int __attribute__((weak)) vm_disk_read_sector(uint32_t lba, void *out512) { (void)lba; (void)out512; return -1; }
int __attribute__((weak)) vm_disk_write_sector(uint32_t lba, const void *in512) { (void)lba; (void)in512; return -1; }

int main(void) {
    uint8_t ram[4096] = {0};
    vm_host_t host;
    vm_arch_state_t state = {0};

    host.mem.ram = ram;
    host.mem.size = sizeof(ram);
    host.cpu.cs = 0x07c0;
    host.cpu.eip = 0;

    vm_io_init();
    vm_arch_collect(&host, &state);

    assert(vm_arch_layers_ready(&state) == 1);
    assert(vm_arch_checkpoints_ready(&state) == 1);
    vm_arch_report(stdout, &state);

    vm_io_shutdown();
    puts("vm arch readiness: OK");
    return 0;
}
