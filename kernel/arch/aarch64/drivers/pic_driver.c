/* AArch64 PIC adapter stub: on ARM this maps to GIC. For now provide no-op host implementation. */
#include "pic_driver.h"
#include "driver_types.h"
#include <stdlib.h>

typedef struct {
    pic_driver_t base;
} pic_impl_t;

static void host_init(pic_driver_t *drv) { (void)drv; }
static void host_eoi(pic_driver_t *drv, int irq) { (void)drv; (void)irq; }

pic_driver_t *pic_driver_create(void) {
    pic_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    impl->base.init = host_init;
    impl->base.eoi = host_eoi;
    impl->base.impl = impl;
    return &impl->base;
}

void pic_driver_destroy(pic_driver_t *drv) {
    free(drv);
}
