#ifndef FL_DRIVERS_H
#define FL_DRIVERS_H

#include "fl/driver/driver_types.h"
#include "block/block_driver.h"

/* Keyboard, display, timer, pic - create/destroy */
keyboard_driver_t *keyboard_driver_create(void);
void keyboard_driver_destroy(keyboard_driver_t *drv);
display_driver_t *display_driver_create(void);
void display_driver_destroy(display_driver_t *drv);
timer_driver_t *timer_driver_create(void);
void timer_driver_destroy(timer_driver_t *drv);
pic_driver_t *pic_driver_create(void);
void pic_driver_destroy(pic_driver_t *drv);

extern block_driver_t    *g_block_driver;
extern keyboard_driver_t *g_keyboard_driver;
extern display_driver_t  *g_display_driver;
extern timer_driver_t    *g_timer_driver;
extern pic_driver_t      *g_pic_driver;

void drivers_init(const char *disk_file);
void drivers_shutdown(void);

#endif
