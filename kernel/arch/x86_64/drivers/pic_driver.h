#ifndef PIC_DRIVER_H
#define PIC_DRIVER_H

#include "driver_types.h"

/* PIC (8259) interrupt controller.
 * Host: no-op. BAREMETAL: init and EOI. */
pic_driver_t *pic_driver_create(void);
void pic_driver_destroy(pic_driver_t *drv);

#endif /* PIC_DRIVER_H */
