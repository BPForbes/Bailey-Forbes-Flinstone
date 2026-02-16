/**
 * Unified bus and device discovery.
 * PCI | DT | SYNTH - drivers match against capabilities, not architecture.
 */
#ifndef FL_DRIVER_BUS_H
#define FL_DRIVER_BUS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bus type for device enumeration */
typedef enum {
    FL_BUS_PCI,
    FL_BUS_DT,    /* Device Tree */
    FL_BUS_SYNTH  /* Synthetic (VM) */
} fl_bus_type_t;

/* MMIO region descriptor */
typedef struct fl_mmio_region {
    uintptr_t phys_base;
    size_t    size;
    volatile void *virt_base;  /* mapped address, or NULL if not mapped */
} fl_mmio_region_t;

#define FL_MMIO_MAX_BARS  6
#define FL_IRQ_MAX        4
#define FL_COMPAT_MAX_LEN  64

/* Device descriptor - unified across all bus types */
typedef struct fl_device_desc {
    fl_bus_type_t bus_type;

    /* PCI identity (when bus_type == FL_BUS_PCI) */
    uint8_t  pci_bus;
    uint8_t  pci_dev;
    uint8_t  pci_fn;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;

    /* DT identity (when bus_type == FL_BUS_DT) */
    char     dt_compatible[FL_COMPAT_MAX_LEN];

    /* Synthetic identity (when bus_type == FL_BUS_SYNTH) */
    char     synth_id[32];

    /* Common: resource locations */
    fl_mmio_region_t mmio[FL_MMIO_MAX_BARS];
    int              mmio_count;
    uint16_t         ioport_base;   /* 0 = none */
    uint16_t         ioport_count;
    int              irq[FL_IRQ_MAX];
    int              irq_count;

    /* Opaque platform data */
    void    *platform_data;
} fl_device_desc_t;

struct fl_device;
typedef struct fl_device fl_device_t;

/* Enumerate all devices on all buses. Returns count, fills descs[]. */
int fl_bus_enumerate(fl_device_desc_t *descs, int max_descs);

/* Get device descriptor for a fl_device handle */
const fl_device_desc_t *fl_device_get_desc(fl_device_t *dev);

/* Create/destroy device handle (used by registry during probe) */
fl_device_t *fl_device_create(const fl_device_desc_t *desc);
void fl_device_destroy(fl_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_BUS_H */
