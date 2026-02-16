#ifndef VM_DRIVERS_H
#define VM_DRIVERS_H

#include "driver_types.h"

/* Global driver instances (host VM view) */
extern block_driver_t    *g_block_driver;
extern keyboard_driver_t *g_keyboard_driver;
extern display_driver_t  *g_display_driver;
extern timer_driver_t    *g_timer_driver;
extern pic_driver_t      *g_pic_driver;

void drivers_init(const char *disk_file);
void drivers_shutdown(void);

#endif /* VM_DRIVERS_H */
