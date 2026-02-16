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

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_DEVICE_H */
