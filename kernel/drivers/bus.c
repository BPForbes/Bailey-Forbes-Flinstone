/**
 * Unified bus enumeration - PCI, DT, or SYNTH.
 * Returns device descriptors for driver matching.
 */
#include "fl/driver/drivers.h"
#include <stdlib.h>
#include <string.h>

int fl_bus_enumerate(fl_device_desc_t *descs, int max_descs) {
    if (!descs || max_descs < 1) return 0;
#if defined(DRIVERS_BAREMETAL)
    /* Bare metal: would probe PCI, DT. Stub for now. */
    (void)descs;
    (void)max_descs;
    return 0;
#else
    /* Host: one synthetic block device */
    extern char current_disk_file[64];
    memset(&descs[0], 0, sizeof(fl_device_desc_t));
    descs[0].bus_type = FL_BUS_SYNTH;
    strncpy(descs[0].synth_id, "host_blk", sizeof(descs[0].synth_id) - 1);
    descs[0].platform_data = (void *)current_disk_file;
    return 1;
#endif
}

const fl_device_desc_t *fl_device_get_desc(fl_device_t *dev) {
    return dev ? &dev->desc : NULL;
}

fl_device_t *fl_device_create(const fl_device_desc_t *desc) {
    if (!desc) return NULL;
    fl_device_t *dev = (fl_device_t *)calloc(1, sizeof(fl_device_t));
    if (!dev) return NULL;
    memcpy(&dev->desc, desc, sizeof(fl_device_desc_t));
    return dev;
}

void fl_device_destroy(fl_device_t *dev) {
    free(dev);
}
