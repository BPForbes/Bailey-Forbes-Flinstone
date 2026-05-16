/**
 * Canonical driver interface - shared across x86-64, aarch64, VM.
 * One driver API, many backends. Architecture-neutral.
 */
#ifndef FL_DRIVER_DRIVER_H
#define FL_DRIVER_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "bus.h"
#include "fl/contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Driver lifecycle states */
typedef enum {
    FL_DRV_STATE_NONE,
    FL_DRV_STATE_PROBED,
    FL_DRV_STATE_ATTACHED,
    FL_DRV_STATE_STARTED,
    FL_DRV_STATE_STOPPED
} fl_driver_state_t;

/* Driver class - matches against bus device class/subclass or compatible strings */
typedef enum {
    FL_DRV_CLASS_NONE = -1,
    FL_DRV_CLASS_BLOCK,
    FL_DRV_CLASS_CONSOLE,   /* display + keyboard */
    FL_DRV_CLASS_TIMER,
    FL_DRV_CLASS_IRQ,
    FL_DRV_CLASS_NET
} fl_driver_class_t;

/* Opaque device handle from bus layer (typedef in bus.h, struct in device.h) */
struct fl_device;

/*
 * Driver operations — fallible lifecycle returns **fl_result_t** (**P0-2**).
 * `probe`: **FL_RESULT_OK** = claim device; **FL_RESULT_PROBE_SKIP** = not this driver;
 *   other negatives = probe failed definitively.
 * `attach`: **FL_RESULT_OK** on success; non-OK rolls back without **start**.
 * `start` / `stop` / `detach`: void — errors are handled internally or ignored.
 */
typedef struct fl_driver_ops {
    fl_result_t (*probe)(fl_device_t *dev);
    fl_result_t (*attach)(fl_device_t *dev);
    void (*start)(fl_device_t *dev);
    void (*stop)(fl_device_t *dev);
    void (*detach)(fl_device_t *dev);
} fl_driver_ops_t;

/* Driver descriptor - one per driver implementation */
typedef struct fl_driver_desc {
    fl_driver_class_t class;
    const char       *name;
    const fl_driver_ops_t *ops;
    /* Match criteria: PCI class/subclass, DT compatible, or synth id */
    uint16_t pci_class;      /* 0 = any */
    uint16_t pci_subclass;   /* 0 = any */
    const char *dt_compatible;  /* NULL = not DT */
    const char *synth_id;    /* NULL = not synthetic */
} fl_driver_desc_t;

/* Registration */
void fl_driver_registry_register_all(void);
fl_result_t fl_driver_registry_register(const fl_driver_desc_t *desc);
fl_result_t fl_driver_registry_match(const fl_device_desc_t *dev, const fl_driver_desc_t **out);

/* Init flow: register -> enumerate -> probe -> attach -> start */
void fl_drivers_init(void);
void fl_drivers_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_DRIVER_H */
