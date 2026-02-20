/**
 * Driver subsystem - unified init for all platforms.
 */
#include "drivers.h"
#include "common.h"
#include "fl/driver/pci.h"
#include <stdio.h>
#include <stdlib.h>

block_driver_t    *g_block_driver    = NULL;
keyboard_driver_t *g_keyboard_driver = NULL;
display_driver_t  *g_display_driver  = NULL;
timer_driver_t    *g_timer_driver    = NULL;
pic_driver_t      *g_pic_driver      = NULL;

void drivers_report_caps(void) {
    fl_block_caps_t bc;
    int block_real = 0;
    if (g_block_driver && g_block_driver->get_caps(g_block_driver, &bc) == 0)
        block_real = fl_cap_is_real(bc.flags);
    uint32_t kb = keyboard_driver_caps();
    uint32_t disp = display_driver_caps();
    uint32_t tm = timer_driver_caps();
    uint32_t pic = pic_driver_caps();
    uint32_t pci = pci_get_caps();
    printf("[drivers] block: %s | kbd: %s | display: %s | timer: %s | pic: %s | pci: %s\n",
           block_real ? "REAL" : "stub",
           fl_cap_is_real(kb) ? "REAL" : "stub",
           fl_cap_is_real(disp) ? "REAL" : "stub",
           fl_cap_is_real(tm) ? "REAL" : "stub",
           fl_cap_is_real(pic) ? "REAL" : "stub",
           fl_cap_is_real(pci) ? "REAL" : "stub");
#ifdef DRIVERS_BAREMETAL
    if (!block_real || !fl_cap_is_real(kb) || !fl_cap_is_real(disp) || !fl_cap_is_real(tm) || !fl_cap_is_real(pic) || !fl_cap_is_real(pci)) {
        fprintf(stderr, "[drivers] FATAL: One or more drivers are STUB/UNIMPLEMENTED - refuse to pretend hardware works.\n");
    }
#endif
}

int drivers_require_real_block(void) {
    if (!g_block_driver) return -1;
    fl_block_caps_t bc;
    if (g_block_driver->get_caps(g_block_driver, &bc) != 0) return -1;
    return fl_cap_is_real(bc.flags) ? 0 : -1;
}

int drivers_require_real_pci(void) {
    return fl_cap_is_real(pci_get_caps()) ? 0 : -1;
}

int drivers_require_real_keyboard(void) {
    return fl_cap_is_real(keyboard_driver_caps()) ? 0 : -1;
}
int drivers_require_real_display(void) {
    return fl_cap_is_real(display_driver_caps()) ? 0 : -1;
}
int drivers_require_real_timer(void) {
    return fl_cap_is_real(timer_driver_caps()) ? 0 : -1;
}
int drivers_require_real_pic(void) {
    return fl_cap_is_real(pic_driver_caps()) ? 0 : -1;
}

int drivers_all_real_for_vm(void) {
    if (drivers_require_real_keyboard() != 0) return -1;
    if (drivers_require_real_display() != 0) return -1;
    if (drivers_require_real_timer() != 0) return -1;
    if (drivers_require_real_pic() != 0) return -1;
    /* Block: optional (VM can use vm_disk); PIC: no-op on host but must exist */
    return 0;
}

int driver_probe_block(void) {
    return drivers_require_real_block();
}
int driver_probe_keyboard(void) {
    return fl_cap_is_real(keyboard_driver_caps()) ? 0 : -1;
}
int driver_probe_display(void) {
    return fl_cap_is_real(display_driver_caps()) ? 0 : -1;
}
int driver_probe_timer(void) {
    return fl_cap_is_real(timer_driver_caps()) ? 0 : -1;
}
int driver_probe_pic(void) {
    return fl_cap_is_real(pic_driver_caps()) ? 0 : -1;
}
int driver_probe_pci(void) {
    return drivers_require_real_pci();
}

static int do_selftest_block(void) {
    if (!g_block_driver) return 0;  /* No block driver - skip */
    uint8_t buf[512];
    return g_block_driver->read_sector((block_driver_t *)g_block_driver, 0, buf) == 0 ? 0 : -1;
}
static int do_selftest_timer(void) {
    if (!g_timer_driver) return -1;
    uint64_t t0 = g_timer_driver->tick_count(g_timer_driver);
    g_timer_driver->msleep(g_timer_driver, 2);
    uint64_t t1 = g_timer_driver->tick_count(g_timer_driver);
    return (t1 >= t0) ? 0 : -1;
}
static int do_selftest_display(void) {
    if (!g_display_driver) return -1;
    g_display_driver->putchar(g_display_driver, ' ');
    return 0;
}

int driver_selftest_block(void) { return do_selftest_block(); }
int driver_selftest_timer(void) { return do_selftest_timer(); }
int driver_selftest_display(void) { return do_selftest_display(); }

int drivers_run_selftest(void) {
    if (driver_selftest_block() != 0) {
        fprintf(stderr, "FATAL: block driver selftest failed\n");
        return -1;
    }
    if (driver_selftest_timer() != 0) {
        fprintf(stderr, "FATAL: timer driver selftest failed\n");
        return -1;
    }
    if (driver_selftest_display() != 0) {
        fprintf(stderr, "FATAL: display driver selftest failed\n");
        return -1;
    }
    return 0;
}

void drivers_init(const char *disk_file) {
    g_block_driver = block_driver_create_host(disk_file ? disk_file : current_disk_file);
    g_keyboard_driver = keyboard_driver_create();
    g_display_driver = display_driver_create();
    g_timer_driver = timer_driver_create();
    g_pic_driver = pic_driver_create();
    if (g_pic_driver && g_pic_driver->init)
        g_pic_driver->init(g_pic_driver);
    drivers_report_caps();
    if (drivers_run_selftest() != 0) {
        fprintf(stderr, "FATAL: Driver selftest failed - refusing to continue.\n");
        exit(1);
    }
}

void drivers_shutdown(void) {
    if (g_block_driver)    { block_driver_destroy(g_block_driver);    g_block_driver = NULL; }
    if (g_keyboard_driver) { keyboard_driver_destroy(g_keyboard_driver); g_keyboard_driver = NULL; }
    if (g_display_driver)  { display_driver_destroy(g_display_driver);  g_display_driver = NULL; }
    if (g_timer_driver)    { timer_driver_destroy(g_timer_driver);    g_timer_driver = NULL; }
    if (g_pic_driver)      { pic_driver_destroy(g_pic_driver);      g_pic_driver = NULL; }
}
