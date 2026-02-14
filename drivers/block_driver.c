/* Block driver - sector-level I/O.
 * Host: uses file I/O. BAREMETAL: would use port_io for IDE. */
#include "block_driver.h"
#include "common.h"
#include "disk.h"
#include "disk_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    block_driver_t base;
    FILE *fp;
    uint32_t sectors;
    int cluster_size;
} block_host_impl_t;

static int host_read_sector(block_driver_t *drv, uint32_t lba, void *buf) {
    block_host_impl_t *impl = (block_host_impl_t *)drv;
    if (!impl->fp || lba >= impl->sectors) return -1;
    /* Our disk format: cluster = sector. Read cluster lba into buf. */
    if (disk_asm_read_cluster((int)lba, (unsigned char *)buf) != 0)
        return -1;
    return 0;
}

static int host_write_sector(block_driver_t *drv, uint32_t lba, const void *buf) {
    block_host_impl_t *impl = (block_host_impl_t *)drv;
    if (!impl->fp || lba >= impl->sectors) return -1;
    if (disk_asm_write_cluster((int)lba, (const unsigned char *)buf) != 0)
        return -1;
    return 0;
}

block_driver_t *block_driver_create_host(const char *disk_file) {
    const char *path = disk_file ? disk_file : current_disk_file;
    strncpy(current_disk_file, path, sizeof(current_disk_file) - 1);
    current_disk_file[sizeof(current_disk_file) - 1] = '\0';
    read_disk_header();
    block_host_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    impl->base.read_sector = host_read_sector;
    impl->base.write_sector = host_write_sector;
    impl->base.impl = impl;
    impl->fp = fopen(path, "r+b");
    if (!impl->fp) impl->fp = fopen(path, "rb");
    impl->sectors = (uint32_t)g_total_clusters;
    impl->cluster_size = g_cluster_size;
    impl->base.sector_count = impl->sectors;
    return &impl->base;
}

void block_driver_destroy(block_driver_t *drv) {
    if (!drv) return;
    block_host_impl_t *impl = (block_host_impl_t *)drv;
    if (impl->fp) fclose(impl->fp);
    free(impl);
}
