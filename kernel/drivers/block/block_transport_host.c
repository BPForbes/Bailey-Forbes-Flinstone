/**
 * Host HAL: Block transport via disk file (disk_asm).
 * Platform-neutral - same for x86-64 and ARM.
 */
#include "fl/driver/block.h"
#include "disk.h"
#include "disk_asm.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t sector_count;
    int      cluster_size;
} host_blk_ctx_t;

static int host_block_read(void *hal_ctx, uint32_t lba, void *buf) {
    (void)hal_ctx;
    return disk_asm_read_cluster((int)lba, (unsigned char *)buf) == 0 ? 0 : -1;
}

static int host_block_write(void *hal_ctx, uint32_t lba, const void *buf) {
    (void)hal_ctx;
    return disk_asm_write_cluster((int)lba, (const unsigned char *)buf) == 0 ? 0 : -1;
}

static int host_block_get_sector_count(void *hal_ctx) {
    host_blk_ctx_t *ctx = (host_blk_ctx_t *)hal_ctx;
    return (int)ctx->sector_count;
}

int fl_hal_block_create_host(const char *disk_file, fl_hal_block_transport_t *out) {
    if (!out || !disk_file) return -1;
    read_disk_header();
    host_blk_ctx_t *ctx = (host_blk_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return -1;
    strncpy(current_disk_file, disk_file, sizeof(current_disk_file) - 1);
    current_disk_file[sizeof(current_disk_file) - 1] = '\0';
    ctx->sector_count = (uint32_t)g_total_clusters;
    ctx->cluster_size = g_cluster_size;
    out->read = host_block_read;
    out->write = host_block_write;
    out->get_sector_count = host_block_get_sector_count;
    out->hal_ctx = ctx;
    return 0;
}

void fl_hal_block_destroy_host(fl_hal_block_transport_t *transport) {
    if (transport && transport->hal_ctx) {
        free(transport->hal_ctx);
        transport->hal_ctx = NULL;
    }
}
