/**
 * Unified block driver - uses HAL transport.
 * One implementation for all platforms.
 */
#include "fl/driver/block.h"
#include "fl/driver/caps.h"
#include "fl/driver/driver_types.h"
#include "block_driver.h"
#include <stdlib.h>
#include <string.h>

/* Forward decl for host transport */
int fl_hal_block_create_host(const char *disk_file, fl_hal_block_transport_t *out);
void fl_hal_block_destroy_host(fl_hal_block_transport_t *transport);

typedef struct {
    fl_block_driver_t base;
    fl_hal_block_transport_t transport;
    int host_owns_transport;
} block_impl_t;

static int block_read_sector(fl_block_driver_t *drv, uint32_t lba, void *buf) {
    block_impl_t *impl = (block_impl_t *)drv;
    return impl->transport.read(impl->transport.hal_ctx, lba, buf);
}

static int block_write_sector(fl_block_driver_t *drv, uint32_t lba, const void *buf) {
    block_impl_t *impl = (block_impl_t *)drv;
    return impl->transport.write(impl->transport.hal_ctx, lba, buf);
}

static int block_get_caps(fl_block_driver_t *drv, fl_block_caps_t *out) {
    block_impl_t *impl = (block_impl_t *)drv;
    if (!out) return -1;
    int sc = impl->transport.get_sector_count(impl->transport.hal_ctx);
    out->max_sector = sc > 0 ? (uint32_t)(sc - 1) : 0;
    out->sector_size = FL_SECTOR_SIZE;
    out->max_transfer = 1;
    out->flags = FL_BLOCK_CAP_READ | FL_BLOCK_CAP_WRITE | FL_CAP_REAL;
    return 0;
}

fl_block_driver_t *fl_block_driver_create(const fl_hal_block_transport_t *transport) {
    if (!transport || !transport->read || !transport->write || !transport->get_sector_count)
        return NULL;
    block_impl_t *impl = (block_impl_t *)calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    memcpy(&impl->transport, transport, sizeof(fl_hal_block_transport_t));
    impl->base.read_sector = block_read_sector;
    impl->base.write_sector = block_write_sector;
    impl->base.get_caps = block_get_caps;
    impl->base.sector_count = (uint32_t)transport->get_sector_count(transport->hal_ctx);
    impl->base.impl = impl;
    return &impl->base;
}

void fl_block_driver_destroy(fl_block_driver_t *drv) {
    if (drv) free(drv);
}

block_driver_t *block_driver_create_host(const char *disk_file) {
    fl_hal_block_transport_t transport;
    if (fl_hal_block_create_host(disk_file, &transport) != 0)
        return NULL;
    block_impl_t *impl = (block_impl_t *)calloc(1, sizeof(*impl));
    if (!impl) {
        fl_hal_block_destroy_host(&transport);
        return NULL;
    }
    memcpy(&impl->transport, &transport, sizeof(transport));
    impl->host_owns_transport = 1;
    impl->base.read_sector = block_read_sector;
    impl->base.write_sector = block_write_sector;
    impl->base.get_caps = block_get_caps;
    impl->base.sector_count = (uint32_t)transport.get_sector_count(transport.hal_ctx);
    impl->base.impl = impl;
    return (block_driver_t *)&impl->base;
}

void block_driver_destroy(block_driver_t *drv) {
    if (!drv) return;
    block_impl_t *impl = (block_impl_t *)drv;
    if (impl->host_owns_transport)
        fl_hal_block_destroy_host(&impl->transport);
    free(impl);
}
