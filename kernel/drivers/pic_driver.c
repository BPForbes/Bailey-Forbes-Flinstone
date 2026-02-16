/**
 * Unified PIC driver - uses fl_ioport for baremetal 8259.
 * Host: no-op. BAREMETAL: PIC port I/O via HAL.
 */
#include "drivers.h"
#include "fl/driver/console.h"
#include "fl/driver/ioport.h"
#include "fl/driver/caps.h"
#include "fl/driver/driver_types.h"
#include <stdlib.h>

#ifdef DRIVERS_BAREMETAL
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define ICW1_INIT 0x11
#define ICW4_8086 0x01
#define PIC_EOI   0x20
#endif

typedef struct {
    pic_driver_t base;
} pic_impl_t;

static void host_init(pic_driver_t *drv) {
    (void)drv;
}

static void host_eoi(pic_driver_t *drv, int irq) {
    (void)drv;
    (void)irq;
}

#ifdef DRIVERS_BAREMETAL
#if defined(__x86_64__) || defined(__i386__)
static void hw_init(pic_driver_t *drv) {
    (void)drv;
    fl_ioport_out8(PIC1_CMD, ICW1_INIT);
    fl_ioport_out8(PIC2_CMD, ICW1_INIT);
    fl_ioport_out8(PIC1_DATA, 0x20);
    fl_ioport_out8(PIC2_DATA, 0x28);
    fl_ioport_out8(PIC1_DATA, 0x04);
    fl_ioport_out8(PIC2_DATA, 0x02);
    fl_ioport_out8(PIC1_DATA, ICW4_8086);
    fl_ioport_out8(PIC2_DATA, ICW4_8086);
    fl_ioport_out8(PIC1_DATA, 0xFF);
    fl_ioport_out8(PIC2_DATA, 0xFF);
}

static void hw_eoi(pic_driver_t *drv, int irq) {
    (void)drv;
    fl_ioport_out8(PIC1_CMD, PIC_EOI);
    if (irq >= 8)
        fl_ioport_out8(PIC2_CMD, PIC_EOI);
}
#elif defined(__aarch64__)
#include "hal/arm_gic.h"
static void hw_init(pic_driver_t *drv) {
    (void)drv;
    arm_gic_init();
}

static void hw_eoi(pic_driver_t *drv, int irq) {
    (void)drv;
    arm_gic_eoi(irq);
}
#endif
#endif

pic_driver_t *pic_driver_create(void) {
    pic_impl_t *impl = (pic_impl_t *)calloc(1, sizeof(*impl));
    if (!impl) return NULL;
#ifdef DRIVERS_BAREMETAL
    impl->base.init = hw_init;
    impl->base.eoi = hw_eoi;
#else
    impl->base.init = host_init;
    impl->base.eoi = host_eoi;
#endif
    impl->base.impl = impl;
    return &impl->base;
}

void pic_driver_destroy(pic_driver_t *drv) {
    free(drv);
}

uint32_t pic_driver_caps(void) {
    if (!g_pic_driver) return 0;
#ifndef DRIVERS_BAREMETAL
    return FL_CAP_REAL;
#elif defined(__x86_64__) || defined(__i386__) || defined(__aarch64__)
    return FL_CAP_REAL;
#else
    return FL_CAP_STUB;
#endif
}
