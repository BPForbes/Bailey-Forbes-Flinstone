#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "VM/devices/vm_io.h"
#include "VM/devices/vm_mem.h"
#include "fl/syscall.h"
#include "drivers.h"

#define VM_SYS_PORT_NO      0xE0
#define VM_SYS_PORT_ARG0    0xE1
#define VM_SYS_PORT_ARG1    0xE2
#define VM_SYS_PORT_ARG2    0xE3
#define VM_SYS_PORT_CALL    0xE5
#define VM_SYS_PORT_RET     0xE6
#define VM_SYS_PORT_ARG0_HI 0xE7
#define VM_SYS_PORT_RET_HI  0xEB

/* Satisfy vm_io link-time globals for this focused bridge test. */
block_driver_t    *g_block_driver = NULL;
keyboard_driver_t *g_keyboard_driver = NULL;
display_driver_t  *g_display_driver = NULL;
timer_driver_t    *g_timer_driver = NULL;
pic_driver_t      *g_pic_driver = NULL;

/* Minimal host/disk shims used by vm_io in this test. */
typedef struct vm_host vm_host_t;
int __attribute__((weak)) vm_host_kbd_pop(vm_host_t *host, uint8_t *out) { (void)host; (void)out; return -1; }
uint64_t __attribute__((weak)) vm_host_ticks(vm_host_t *host) { (void)host; return 0; }
int __attribute__((weak)) vm_disk_is_active(void) { return 0; }
int __attribute__((weak)) vm_disk_read_sector(uint32_t lba, void *out512) { (void)lba; (void)out512; return -1; }
int __attribute__((weak)) vm_disk_write_sector(uint32_t lba, const void *in512) { (void)lba; (void)in512; return -1; }

static void write_sys_arg64(uint32_t arg, uintptr_t value) {
    vm_io_out(NULL, VM_SYS_PORT_ARG0 + arg, (uint32_t)value, 4);
    vm_io_out(NULL, VM_SYS_PORT_ARG0_HI + arg, (uint32_t)(value >> 32), 4);
}

static long read_sys_ret(void) {
    uint32_t lo = vm_io_in(NULL, VM_SYS_PORT_RET, 4);
    uint32_t hi = vm_io_in(NULL, VM_SYS_PORT_RET_HI, 4);
    return (long)(intptr_t)(((uintptr_t)hi << 32) | lo);
}

int main(void) {
    uint8_t ram[256] = {0};
    vm_mem_t mem = { .ram = ram, .size = sizeof(ram) };

    vm_io_init();

    vm_io_out(NULL, VM_SYS_PORT_NO, FL_SYS_PIPE_CREATE, 4);
    write_sys_arg64(0, 64);
    vm_io_out(NULL, VM_SYS_PORT_CALL, 0, 4);
    long h = read_sys_ret();
    assert(h >= 0);

    const char *msg = "guest-bridge";
    memcpy(ram + 16, msg, strlen(msg));
    vm_io_out(&mem, VM_SYS_PORT_NO, FL_SYS_PIPE_WRITE, 4);
    write_sys_arg64(0, (uintptr_t)h);
    write_sys_arg64(1, 16);
    write_sys_arg64(2, strlen(msg));
    vm_io_out(&mem, VM_SYS_PORT_CALL, 0, 4);
    assert(read_sys_ret() == (long)strlen(msg));

    vm_io_out(&mem, VM_SYS_PORT_NO, FL_SYS_PIPE_READ, 4);
    write_sys_arg64(0, (uintptr_t)h);
    write_sys_arg64(1, 64);
    write_sys_arg64(2, strlen(msg));
    vm_io_out(&mem, VM_SYS_PORT_CALL, 0, 4);
    assert(read_sys_ret() == (long)strlen(msg));
    assert(memcmp(ram + 64, msg, strlen(msg)) == 0);

    vm_io_out(NULL, VM_SYS_PORT_NO, FL_SYS_CLOSE, 4);
    write_sys_arg64(0, (uintptr_t)h);
    vm_io_out(NULL, VM_SYS_PORT_CALL, 0, 4);
    long rc = read_sys_ret();
    assert(rc >= 0);
    assert(rc == 0);

    vm_io_out(NULL, VM_SYS_PORT_NO, FL_SYS_CLOSE, 4);
    write_sys_arg64(0, (uintptr_t)h);
    vm_io_out(NULL, VM_SYS_PORT_CALL, 0, 4);
    long err = read_sys_ret();
    assert(err == -1);
    assert(vm_io_in(NULL, VM_SYS_PORT_RET, 1) == 0xFF);
    assert(vm_io_in(NULL, VM_SYS_PORT_RET, 2) == 0xFFFF);

    vm_io_shutdown();
    puts("vm syscall bridge: OK");
    return 0;
}
