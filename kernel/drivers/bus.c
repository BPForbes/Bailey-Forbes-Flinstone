/**
 * Unified bus enumeration - PCI, DT, or SYNTH.
 * Returns device descriptors for driver matching.
 */
#include "fl/driver/drivers.h"
#include "fl/mm.h"
#include "fl/mem_asm.h"

static size_t fl_cstr_len(const char *s, size_t max_len) {
    size_t n = 0;
    if (!s)
        return 0;
    while (n < max_len && s[n])
        n++;
    return n;
}

static void fl_cstr_copy(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0)
        return;
    size_t n = fl_cstr_len(src, dst_len - 1);
    if (n)
        asm_mem_copy(dst, src, n);
    dst[n] = '\0';
}

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
    descs[0].irq[0] = 3;
    descs[0].irq_count = 1;
    descs[0].resources[0].type = FL_RESOURCE_SYNTH;
    descs[0].resources[0].start = (uintptr_t)current_disk_file;
    descs[0].resources[0].size = fl_cstr_len(current_disk_file, 256) + 1;
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
