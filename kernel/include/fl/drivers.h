#ifndef FL_DRIVERS_H
#define FL_DRIVERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Driver types */
typedef enum {
    DRIVER_BLOCK,
    DRIVER_KEYBOARD,
    DRIVER_DISPLAY,
    DRIVER_TIMER,
    DRIVER_NETWORK
} driver_type_t;

/* Driver capabilities */
typedef struct {
    uint32_t block_size;
    uint64_t total_blocks;
} block_driver_caps_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
} display_driver_caps_t;

/* Driver operations */
typedef struct {
    int (*init)(void);
    void (*shutdown)(void);
    int (*read)(void *buf, size_t count);
    int (*write)(const void *buf, size_t count);
} driver_ops_t;

/* Driver registration */
int driver_register(driver_type_t type, const driver_ops_t *ops, void *caps);
void driver_unregister(driver_type_t type);

/* Driver access */
void *driver_get_caps(driver_type_t type);
int driver_read(driver_type_t type, void *buf, size_t count);
int driver_write(driver_type_t type, const void *buf, size_t count);

/* Driver initialization */
void drivers_init(void);
void drivers_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVERS_H */
