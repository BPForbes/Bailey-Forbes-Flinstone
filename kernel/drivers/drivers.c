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

void drivers_init(const char *disk_file) {
    g_block_driver = block_driver_create_host(disk_file ? disk_file : current_disk_file);
    g_keyboard_driver = keyboard_driver_create();
    g_display_driver = display_driver_create();
    g_timer_driver = timer_driver_create();
    g_pic_driver = pic_driver_create();
    if (g_pic_driver && g_pic_driver->init)
        g_pic_driver->init(g_pic_driver);
    drivers_report_caps();
}

void drivers_shutdown(void) {
    if (g_block_driver)    { block_driver_destroy(g_block_driver);    g_block_driver = NULL; }
    if (g_keyboard_driver) { keyboard_driver_destroy(g_keyboard_driver); g_keyboard_driver = NULL; }
    if (g_display_driver)  { display_driver_destroy(g_display_driver);  g_display_driver = NULL; }
    if (g_timer_driver)    { timer_driver_destroy(g_timer_driver);    g_timer_driver = NULL; }
    if (g_pic_driver)      { pic_driver_destroy(g_pic_driver);      g_pic_driver = NULL; }
}
