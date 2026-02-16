/**
 * ARM GIC v2 - real MMIO init and EOI (QEMU virt).
 * GICD base 0x08000000, GICC base 0x08010000.
 */
#include "arm_gic.h"
#include "arm_plat.h"
#include "fl/driver/mmio.h"

#define GICD_BASE ARM_GICD_BASE
#define GICC_BASE ARM_GICC_BASE
#define GICD_CTLR      0x000
#define GICD_IGROUPR0  0x080
#define GICC_CTLR      0x000
#define GICC_PMR       0x004
#define GICC_EOIR      0x010

static volatile void *gicd(void) { return (volatile void *)GICD_BASE; }
static volatile void *gicc(void) { return (volatile void *)GICC_BASE; }

void arm_gic_init(void) {
    volatile void *d = gicd();
    volatile void *c = gicc();
    fl_mmio_write32((void *)((char *)d + GICD_CTLR), 1);
    fl_mmio_write32((void *)((char *)d + GICD_IGROUPR0), 0);
    fl_mmio_write32((void *)((char *)c + GICC_PMR), 0xFF);
    fl_mmio_write32((void *)((char *)c + GICC_CTLR), 1);
}

void arm_gic_eoi(int irq) {
    (void)irq;
    volatile void *c = gicc();
    fl_mmio_write32((void *)((char *)c + GICC_EOIR), 1023);
}
