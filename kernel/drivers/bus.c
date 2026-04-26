/**
 * Unified bus enumeration - PCI, DT, or SYNTH.
 * Returns device descriptors for driver matching.
 */
#include "fl/driver/drivers.h"
#include "fl/mm.h"
#include "fl/mem_asm.h"
#include "common.h"
#include "fl_cstr.h"

int fl_bus_enumerate(fl_device_desc_t *descs, int max_descs) {
    if (!descs || max_descs < 1) return 0;
#if defined(DRIVERS_BAREMETAL)
    /* Bare metal: would probe PCI, DT. Stub for now. */
    (void)descs;
    (void)max_descs;
    return 0;
#else
    /* Host: one synthetic block device */
    extern char current_disk_file[];
    asm_mem_zero(&descs[0], sizeof(fl_device_desc_t));
    descs[0].bus_type = FL_BUS_SYNTH;
    fl_cstr_copy(descs[0].synth_id, sizeof(descs[0].synth_id), "host_blk");
    descs[0].resources[0].type = FL_RESOURCE_SYNTH;
    descs[0].resources[0].start = (uintptr_t)current_disk_file;
    descs[0].resources[0].size = fl_cstr_len(current_disk_file, CWD_MAX) + 1;
    fl_cstr_copy(descs[0].resources[0].name, sizeof(descs[0].resources[0].name), "host-disk");
    descs[0].resources[1].type = FL_RESOURCE_IRQ;
    descs[0].resources[1].start = 3;
    descs[0].resources[1].size = 1;
    fl_cstr_copy(descs[0].resources[1].name, sizeof(descs[0].resources[1].name), "host-irq");
    descs[0].resources[2].type = FL_RESOURCE_DMA;
    descs[0].resources[2].start = 0;
    descs[0].resources[2].size = FL_SECTOR_SIZE;
    fl_cstr_copy(descs[0].resources[2].name, sizeof(descs[0].resources[2].name), "block-dma");
    descs[0].resources[3].type = FL_RESOURCE_IOPORT;
    descs[0].resources[3].start = 0xF100u;
    descs[0].resources[3].size = 8;
    fl_cstr_copy(descs[0].resources[3].name, sizeof(descs[0].resources[3].name), "synth-io");
    descs[0].resource_count = 4;
    descs[0].platform_data = (void *)current_disk_file;
    return 1;
#endif
}

const fl_device_desc_t *fl_device_get_desc(const fl_device_t *dev) {
    return dev ? &dev->desc : NULL;
}

fl_device_t *fl_device_create(const fl_device_desc_t *desc) {
    if (!desc) return NULL;
    fl_device_t *dev = (fl_device_t *)kmalloc(sizeof(fl_device_t));
    if (!dev) return NULL;
    asm_mem_zero(dev, sizeof(*dev));
    asm_mem_copy(&dev->desc, desc, sizeof(fl_device_desc_t));
    return dev;
}

void fl_device_destroy(fl_device_t *dev) {
    kfree(dev);
}

int fl_device_desc_irq_list(const fl_device_desc_t *desc, int *irq, int max_irq) {
    if (!desc || !irq || max_irq <= 0)
        return 0;
    int count = 0;
    for (int i = 0; i < desc->resource_count && count < max_irq; i++) {
        if (desc->resources[i].type == FL_RESOURCE_IRQ)
            irq[count++] = (int)desc->resources[i].start;
    }
    return count;
}

int fl_device_desc_mmio_list(const fl_device_desc_t *desc, fl_mmio_region_t *mmio, int max_mmio) {
    if (!desc || !mmio || max_mmio <= 0)
        return 0;
    int count = 0;
    for (int i = 0; i < desc->resource_count && count < max_mmio; i++) {
        if (desc->resources[i].type != FL_RESOURCE_MMIO)
            continue;
        mmio[count].phys_base = desc->resources[i].start;
        mmio[count].size = desc->resources[i].size;
        mmio[count].virt_base = NULL;
        count++;
    }
    return count;
}

int fl_device_desc_ioport_range(const fl_device_desc_t *desc, uint16_t *base, uint16_t *count) {
    if (!desc || !base || !count)
        return -1;
    *base = 0;
    *count = 0;
    for (int i = 0; i < desc->resource_count; i++) {
        if (desc->resources[i].type != FL_RESOURCE_IOPORT)
            continue;
        *base = (uint16_t)desc->resources[i].start;
        *count = (uint16_t)desc->resources[i].size;
        return 0;
    }
    return -1;
}
