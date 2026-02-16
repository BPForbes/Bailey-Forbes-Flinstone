/* AArch64 block driver stub - delegate to disk_asm or use host file I/O */
#include "block_driver.h"
#include "driver_caps.h"
#include "common.h"
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
    (void)drv;
    if (disk_asm_read_cluster((int)lba, (unsigned char *)buf) != 0)
        return -1;
    return 0;
}

static int host_write_sector(block_driver_t *drv, uint32_t lba, const void *buf) {
    (void)drv;
    if (disk_asm_write_cluster((int)lba, (const unsigned char *)buf) != 0)
        return -1;
    return 0;
}

static int host_get_caps(block_driver_t *drv, block_caps_t *out) {
    if (!out) return -1;
    out->max_sector = g_total_clusters > 0 ? (uint32_t)g_total_clusters - 1 : 0;
    out->sector_size = (uint32_t)g_cluster_size;
    out->max_transfer = 1;
    out->flags = CAP_READ | CAP_WRITE;
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
    impl->base.get_caps = host_get_caps;
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
