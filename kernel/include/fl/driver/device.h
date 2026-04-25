/**
 * Device handle - represents an enumerated device bound to a driver.
 */
#ifndef FL_DRIVER_DEVICE_H
#define FL_DRIVER_DEVICE_H

#include "bus.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fl_device {
    fl_device_desc_t desc;
    void *driver_data;   /* driver-private data set during attach */
    void *hal_data;      /* HAL-private transport handle */
};

typedef struct fl_device_info {
    const fl_device_t *dev;
    const fl_device_desc_t *desc;
    const char *driver_name;
    int driver_class;
    int state;
} fl_device_info_t;

int fl_device_count(void);
int fl_device_get_info(int index, fl_device_info_t *out);
fl_device_t *fl_device_find_synth(const char *synth_id);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_DEVICE_H */
