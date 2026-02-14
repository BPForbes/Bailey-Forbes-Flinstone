#ifndef DRIVERS_H
#define DRIVERS_H

#include "block_driver.h"
#include "keyboard_driver.h"
#include "display_driver.h"
#include "timer_driver.h"
#include "pic_driver.h"

/* Global driver instances (optional - use when drivers are active) */
extern block_driver_t    *g_block_driver;
extern keyboard_driver_t *g_keyboard_driver;
extern display_driver_t  *g_display_driver;
extern timer_driver_t    *g_timer_driver;
extern pic_driver_t      *g_pic_driver;

void drivers_init(const char *disk_file);
void drivers_shutdown(void);

#endif /* DRIVERS_H */
