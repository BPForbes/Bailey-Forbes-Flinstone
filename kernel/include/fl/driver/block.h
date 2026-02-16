/**
 * Block device interface - one API, HAL provides transport.
 * Matches: PCI MassStorage (SATA/NVMe), DT virtio-blk, synth vm_blk.
 */
#ifndef FL_DRIVER_BLOCK_H
#define FL_DRIVER_BLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_SECTOR_SIZE  512

/* Block capabilities */
typedef struct fl_block_caps {
    uint32_t max_sector;
    uint32_t sector_size;
    uint32_t max_transfer;
    uint32_t flags;  /* FL_BLOCK_CAP_READ | FL_BLOCK_CAP_WRITE */
} fl_block_caps_t;

#define FL_BLOCK_CAP_READ  1
#define FL_BLOCK_CAP_WRITE 2

/* Block driver vtable - platform-neutral logic uses this */
typedef struct fl_block_driver fl_block_driver_t;
struct fl_block_driver {
    int (*read_sector)(fl_block_driver_t *drv, uint32_t lba, void *buf);
    int (*write_sector)(fl_block_driver_t *drv, uint32_t lba, const void *buf);
    int (*get_caps)(fl_block_driver_t *drv, fl_block_caps_t *out);
    uint32_t sector_count;
    void *impl;
};

/* HAL transport - implemented by each platform */
typedef struct fl_hal_block_transport {
    int (*read)(void *hal_ctx, uint32_t lba, void *buf);
    int (*write)(void *hal_ctx, uint32_t lba, const void *buf);
    int (*get_sector_count)(void *hal_ctx);
    void *hal_ctx;
} fl_hal_block_transport_t;

/* Create block driver from HAL transport (platform-neutral block_driver.c) */
fl_block_driver_t *fl_block_driver_create(const fl_hal_block_transport_t *transport);
void fl_block_driver_destroy(fl_block_driver_t *drv);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_BLOCK_H */
