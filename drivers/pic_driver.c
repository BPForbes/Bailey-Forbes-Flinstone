/* PIC driver - interrupt controller (8259).
 * Host: no-op. BAREMETAL: init, mask, EOI. */
#include "pic_driver.h"
#include "port_io.h"
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
static void hw_init(pic_driver_t *drv) {
    (void)drv;
    port_outb(PIC1_CMD, ICW1_INIT);
    port_outb(PIC2_CMD, ICW1_INIT);
    port_outb(PIC1_DATA, 0x20);  /* IRQ 0-7 -> int 0x20-0x27 */
    port_outb(PIC2_DATA, 0x28);  /* IRQ 8-15 -> int 0x28-0x2F */
    port_outb(PIC1_DATA, 0x04);  /* slave on IRQ2 */
    port_outb(PIC2_DATA, 0x02);
    port_outb(PIC1_DATA, ICW4_8086);
    port_outb(PIC2_DATA, ICW4_8086);
    port_outb(PIC1_DATA, 0xFF);  /* mask all initially */
    port_outb(PIC2_DATA, 0xFF);
}

static void hw_eoi(pic_driver_t *drv, int irq) {
    (void)drv;
    port_outb(PIC1_CMD, PIC_EOI);
    if (irq >= 8)
        port_outb(PIC2_CMD, PIC_EOI);
}
#endif

pic_driver_t *pic_driver_create(void) {
    pic_impl_t *impl = calloc(1, sizeof(*impl));
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
