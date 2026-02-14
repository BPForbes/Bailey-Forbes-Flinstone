/* Driver subsystem - init all drivers.
 * Host mode: block/keyboard/display/timer use host backends.
 * BAREMETAL: use port_io + hardware. */
#include "drivers.h"
#include "common.h"
#include <stdlib.h>

block_driver_t    *g_block_driver    = NULL;
keyboard_driver_t *g_keyboard_driver = NULL;
display_driver_t  *g_display_driver  = NULL;
timer_driver_t    *g_timer_driver    = NULL;
pic_driver_t      *g_pic_driver      = NULL;

void drivers_init(const char *disk_file) {
    g_block_driver = block_driver_create_host(disk_file ? disk_file : current_disk_file);
    g_keyboard_driver = keyboard_driver_create();
    g_display_driver = display_driver_create();
    g_timer_driver = timer_driver_create();
    g_pic_driver = pic_driver_create();
    if (g_pic_driver && g_pic_driver->init)
        g_pic_driver->init(g_pic_driver);
}

void drivers_shutdown(void) {
    if (g_block_driver)    { block_driver_destroy(g_block_driver);    g_block_driver = NULL; }
    if (g_keyboard_driver) { keyboard_driver_destroy(g_keyboard_driver); g_keyboard_driver = NULL; }
    if (g_display_driver)  { display_driver_destroy(g_display_driver);  g_display_driver = NULL; }
    if (g_timer_driver)    { timer_driver_destroy(g_timer_driver);    g_timer_driver = NULL; }
    if (g_pic_driver)      { pic_driver_destroy(g_pic_driver);      g_pic_driver = NULL; }
}
