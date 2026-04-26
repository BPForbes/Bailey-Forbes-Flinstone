#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "VM/devices/vm_io.h"
#include "fl/syscall.h"
#include "drivers.h"

/* Satisfy vm_io link-time globals for this focused bridge test. */
block_driver_t    *g_block_driver = NULL;
keyboard_driver_t *g_keyboard_driver = NULL;
display_driver_t  *g_display_driver = NULL;
timer_driver_t    *g_timer_driver = NULL;
pic_driver_t      *g_pic_driver = NULL;

/* Minimal host/disk shims used by vm_io in this test. */
typedef struct vm_host vm_host_t;
int vm_host_kbd_pop(vm_host_t *host, uint8_t *out) { (void)host; (void)out; return -1; }
uint64_t vm_host_ticks(vm_host_t *host) { (void)host; return 0; }
int vm_disk_is_active(void) { return 0; }
int vm_disk_read_sector(uint32_t lba, void *out512) { (void)lba; (void)out512; return -1; }
int vm_disk_write_sector(uint32_t lba, const void *in512) { (void)lba; (void)in512; return -1; }

int main(void) {
    vm_io_init();

    vm_io_out(NULL, 0xE0, FL_SYS_PIPE_CREATE, 4);
    vm_io_out(NULL, 0xE1, 64, 4);
    vm_io_out(NULL, 0xE5, 0, 4);
    uint32_t h = vm_io_in(NULL, 0xE6, 4);
    assert(h != 0xFFFFFFFFu);

    vm_io_out(NULL, 0xE0, FL_SYS_CLOSE, 4);
    vm_io_out(NULL, 0xE1, h, 4);
    vm_io_out(NULL, 0xE5, 0, 4);
    uint32_t rc = vm_io_in(NULL, 0xE6, 4);
    assert(rc == 0);

    vm_io_shutdown();
    puts("vm syscall bridge: OK");
    return 0;
}
