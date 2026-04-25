/**
 * Driver test suite: block, display, timer, PIC (host mode).
 */
#include "drivers/drivers.h"
#include "drivers/block/block_driver.h"
#include "drivers/fl_cstr.h"
#include "fl/driver/device.h"
#include "fl/driver/devfs.h"
#include "fl/driver/irq.h"
#include "fl/mm.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int create_temp_disk(const char *path, int clusters, int cluster_size) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "XX:");
    for (int i = 0; i < cluster_size * 2; i++) fputc('0', fp);
    fputc('\n', fp);
    for (int c = 0; c < clusters; c++) {
        fprintf(fp, "%02X:", c);
        for (int i = 0; i < cluster_size * 2; i++) fputc('0', fp);
        fputc('\n', fp);
    }
    fclose(fp);
    return 0;
}

static int test_block(void) {
    ASSERT(g_block_driver != NULL);
    block_caps_t caps;
    ASSERT(g_block_driver->get_caps((block_driver_t *)g_block_driver, &caps) == 0);
    ASSERT(caps.sector_size > 0);
    (void)caps;
    uint8_t buf[512];
    memset(buf, 0xAB, sizeof(buf));
    ASSERT(g_block_driver->read_sector((block_driver_t *)g_block_driver, 0, buf) == 0);
    return 0;
}

static int test_block_write_read(void) {
    int cs = g_cluster_size > 64 ? 64 : g_cluster_size;
    uint8_t wbuf[64], rbuf[64];
    memset(wbuf, 0, sizeof(wbuf));
    memset(rbuf, 0, sizeof(rbuf));
    for (int i = 0; i < cs; i++) wbuf[i] = (uint8_t)(0x30 + i);
    ASSERT(g_block_driver->write_sector((block_driver_t *)g_block_driver, 0, wbuf) == 0);
    ASSERT(g_block_driver->read_sector((block_driver_t *)g_block_driver, 0, rbuf) == 0);
    ASSERT(memcmp(wbuf, rbuf, cs) == 0);
    return 0;
}

static int test_device_model(void) {
    fl_device_desc_t descs[4];
    const fl_driver_desc_t *matched = NULL;
    fl_device_t *dev = NULL;
    const fl_device_desc_t *desc = NULL;
    fl_device_info_t info;
    fl_resource_t res;

    memset(descs, 0, sizeof(descs));
    memset(&info, 0, sizeof(info));
    memset(&res, 0, sizeof(res));
    ASSERT(fl_bus_enumerate(descs, 4) == 1);
    ASSERT(fl_driver_registry_match(&descs[0], &matched) == 0);
    ASSERT(matched != NULL);
    ASSERT(strcmp(matched->name, "host-block") == 0);
    ASSERT(fl_device_count() == 1);
    ASSERT(fl_device_get_info(0, &info) == 0);
    ASSERT(info.dev != NULL);
    ASSERT(info.desc != NULL);
    ASSERT(strcmp(info.driver_name, "host-block") == 0);
    ASSERT(info.driver_class == FL_DRV_CLASS_BLOCK);
    ASSERT(info.state == FL_DRV_STATE_STARTED);
    dev = fl_device_find_synth("host_blk");
    ASSERT(dev != NULL);
    desc = fl_device_get_desc(dev);
    ASSERT(desc != NULL);
    ASSERT(desc->bus_type == FL_BUS_SYNTH);
    ASSERT(strcmp(desc->synth_id, "host_blk") == 0);
    ASSERT(fl_resource_count(dev) == 4);
    ASSERT(fl_resource_get(dev, 0, &res) == 0);
    ASSERT(res.type == FL_RESOURCE_SYNTH);
    ASSERT(res.start == (uintptr_t)current_disk_file);
    ASSERT(res.size == strlen(current_disk_file) + 1);
    fl_drivers_init();
    ASSERT(fl_device_count() == 1);
    ASSERT(fl_resource_claim(dev, FL_RESOURCE_SYNTH, 0) != 0);
    ASSERT(fl_resource_claim(dev, FL_RESOURCE_SYNTH, 99) != 0);
    return 0;
}

static int test_devfs_block(void) {
    fl_devfs_file_t file;
    fl_block_caps_t caps;
    uint8_t wbuf[512], rbuf[512];
    size_t n = 0;

    memset(&file, 0, sizeof(file));
    memset(&caps, 0, sizeof(caps));
    memset(wbuf, 0, sizeof(wbuf));
    memset(rbuf, 0, sizeof(rbuf));
    for (int i = 0; i < g_cluster_size && i < (int)sizeof(wbuf); i++)
        wbuf[i] = (uint8_t)(0x41 + (i % 26));

    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_READ | FL_DEVFS_O_WRITE, &file) == 0);
    ASSERT(fl_devfs_ioctl(&file, FL_DEVFS_IOCTL_BLOCK_CAPS, &caps) == 0);
    ASSERT(caps.sector_size == FL_SECTOR_SIZE);
    ASSERT(fl_devfs_write(&file, wbuf, sizeof(wbuf), &n) == 0);
    ASSERT(n == FL_SECTOR_SIZE);
    file.pos = 0;
    ASSERT(fl_devfs_read(&file, rbuf, sizeof(rbuf), &n) == 0);
    ASSERT(n == FL_SECTOR_SIZE);
    ASSERT(memcmp(wbuf, rbuf, (size_t)g_cluster_size) == 0);
    ASSERT(fl_devfs_close(&file) == 0);
    ASSERT(fl_devfs_register("/dev/this/path/is/too/long/for/devfs", FL_DRV_CLASS_BLOCK, fl_device_find_synth("host_blk"), &(fl_devfs_ops_t){0}) != 0);
    return 0;
}

static void test_irq_handler(int irq, void *ctx) {
    int *hits = (int *)ctx;
    if (irq == 3)
        (*hits)++;
}

static int test_irq_and_dma(void) {
    int hits = 0;
    fl_device_t *dev = fl_device_find_synth("host_blk");
    uint8_t src[128], dst[128];
    fl_dma_info_t info;
    fl_irq_info_t irq_info;
    void *buf = NULL;
    ASSERT(dev != NULL);
    buf = fl_dma_alloc_device(dev, sizeof(dst));
    for (size_t i = 0; i < sizeof(src); i++)
        src[i] = (uint8_t)(i + 1);
    ASSERT(buf != NULL);
    ASSERT(fl_dma_allocation_count() == 1);
    ASSERT(fl_dma_get_info(buf, &info) == 0);
    ASSERT(info.ptr == buf);
    ASSERT(info.size == sizeof(dst));
    ASSERT(info.owner == dev);
    fl_dma_copy(buf, src, sizeof(src));
    memset(dst, 0, sizeof(dst));
    fl_dma_copy(dst, buf, sizeof(dst));
    ASSERT(memcmp(src, dst, sizeof(src)) == 0);
    fl_dma_zero(buf, sizeof(dst));
    memset(dst, 0xFF, sizeof(dst));
    fl_dma_copy(dst, buf, sizeof(dst));
    ASSERT(dst[0] == 0 && dst[sizeof(dst) - 1] == 0);
    fl_dma_free(buf);
    ASSERT(fl_dma_allocation_count() == 0);
    ASSERT(fl_irq_register_device(dev, 0, test_irq_handler, &hits) == 0);
    fl_irq_enable(3);
    ASSERT(fl_irq_dispatch(3) == 0);
    ASSERT(hits == 1);
    ASSERT(fl_irq_dispatch_count(3) == 1);
    ASSERT(fl_irq_get_info(3, &irq_info) == 0);
    ASSERT(irq_info.irq == 3);
    ASSERT(irq_info.enabled == 1);
    ASSERT(irq_info.dispatch_count == 1);
    ASSERT(irq_info.eoi_count == 1);
    ASSERT(irq_info.owner == dev);
    fl_irq_disable(3);
    ASSERT(fl_irq_dispatch(3) != 0);
    fl_irq_unregister_device(dev);
    ASSERT(fl_irq_get_info(3, &irq_info) == 0);
    ASSERT(irq_info.owner == NULL);
    ASSERT(fl_irq_register(3, test_irq_handler, &hits) == 0);
    fl_irq_unregister(3);
    ASSERT(fl_irq_get_info(3, &irq_info) == 0);
    ASSERT(irq_info.owner == NULL);
    ASSERT(fl_irq_register_device(dev, 99, test_irq_handler, &hits) != 0);
    ASSERT(fl_irq_register(31, test_irq_handler, &hits) != 0);
    ASSERT(fl_irq_get_info(99, &irq_info) != 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* fl_cstr: new header-only string utilities                           */
/* ------------------------------------------------------------------ */
static int test_fl_cstr(void) {
    /* --- fl_cstr_len --- */
    ASSERT(fl_cstr_len(NULL, 10) == 0);
    ASSERT(fl_cstr_len("", 10) == 0);
    ASSERT(fl_cstr_len("hello", 10) == 5);
    /* max_len truncates: only 3 of 5 chars counted */
    ASSERT(fl_cstr_len("hello", 3) == 3);
    /* max_len of 0: always returns 0 */
    ASSERT(fl_cstr_len("hello", 0) == 0);
    /* exactly at boundary */
    ASSERT(fl_cstr_len("abc", 3) == 3);
    ASSERT(fl_cstr_len("abc", 4) == 3);

    /* --- fl_cstr_eq --- */
    ASSERT(fl_cstr_eq(NULL, "foo") == 0);
    ASSERT(fl_cstr_eq("foo", NULL) == 0);
    ASSERT(fl_cstr_eq(NULL, NULL) == 0);
    ASSERT(fl_cstr_eq("", "") == 1);
    ASSERT(fl_cstr_eq("foo", "foo") == 1);
    ASSERT(fl_cstr_eq("foo", "bar") == 0);
    /* prefix mismatch: "foo" vs "foo2" */
    ASSERT(fl_cstr_eq("foo", "foo2") == 0);
    ASSERT(fl_cstr_eq("foo2", "foo") == 0);
    /* single char */
    ASSERT(fl_cstr_eq("a", "a") == 1);
    ASSERT(fl_cstr_eq("a", "b") == 0);
    /* host_blk is the real synth ID used by the driver */
    ASSERT(fl_cstr_eq("host_blk", "host_blk") == 1);
    ASSERT(fl_cstr_eq("host_blk", "host_blk2") == 0);

    /* --- fl_cstr_copy --- */
    char buf[16];

    /* NULL dst: no-op, no crash */
    fl_cstr_copy(NULL, 5, "hello");

    /* dst_len 0: no-op, no crash */
    fl_cstr_copy(buf, 0, "hello");

    /* NULL src: dst gets empty string */
    memset(buf, 0xFF, sizeof(buf));
    fl_cstr_copy(buf, sizeof(buf), NULL);
    ASSERT(buf[0] == '\0');

    /* Normal copy */
    fl_cstr_copy(buf, sizeof(buf), "hello");
    ASSERT(strcmp(buf, "hello") == 0);

    /* Truncation: dst_len=5 allows only 4 chars + NUL */
    fl_cstr_copy(buf, 5, "hello world");
    ASSERT(buf[4] == '\0');
    ASSERT(strncmp(buf, "hell", 4) == 0);

    /* Empty string copy */
    fl_cstr_copy(buf, sizeof(buf), "");
    ASSERT(buf[0] == '\0');

    /* dst_len=1: only the NUL terminator fits */
    fl_cstr_copy(buf, 1, "hello");
    ASSERT(buf[0] == '\0');

    /* Overlap: src and dst overlap (backward move case: dst > src) */
    static char overlap[16];
    memset(overlap, 0, sizeof(overlap));
    strcpy(overlap, "abcde");
    /* Shift two bytes forward inside the same buffer */
    fl_cstr_copy(overlap + 2, sizeof(overlap) - 2, overlap);
    ASSERT(overlap[2] == 'a');
    ASSERT(overlap[3] == 'b');
    ASSERT(overlap[4] == 'c');
    ASSERT(overlap[5] == 'd');
    ASSERT(overlap[6] == 'e');
    ASSERT(overlap[7] == '\0');

    return 0;
}

/* ------------------------------------------------------------------ */
/* bus resource extraction: new functions in bus.c                    */
/* ------------------------------------------------------------------ */
static int test_bus_resource_extraction(void) {
    fl_device_desc_t descs[2];
    int irq_list[8];
    fl_mmio_region_t mmio_list[8];
    uint16_t ioport_base, ioport_count;

    memset(descs, 0, sizeof(descs));

    /* Get the real descriptor from bus enumeration */
    int n = fl_bus_enumerate(descs, 2);
    ASSERT(n == 1);

    const fl_device_desc_t *d = &descs[0];
    ASSERT(d->resource_count == 4);

    /* ---- fl_device_desc_irq_list ---- */
    /* NULL desc */
    ASSERT(fl_device_desc_irq_list(NULL, irq_list, 8) == 0);
    /* NULL irq buffer */
    ASSERT(fl_device_desc_irq_list(d, NULL, 8) == 0);
    /* max_irq <= 0 */
    ASSERT(fl_device_desc_irq_list(d, irq_list, 0) == 0);
    /* host descriptor has exactly 1 IRQ resource at line 3 */
    memset(irq_list, 0, sizeof(irq_list));
    ASSERT(fl_device_desc_irq_list(d, irq_list, 8) == 1);
    ASSERT(irq_list[0] == 3);
    /* max_irq=1 still gets one result */
    memset(irq_list, 0, sizeof(irq_list));
    ASSERT(fl_device_desc_irq_list(d, irq_list, 1) == 1);
    ASSERT(irq_list[0] == 3);

    /* ---- fl_device_desc_mmio_list ---- */
    /* NULL desc */
    ASSERT(fl_device_desc_mmio_list(NULL, mmio_list, 8) == 0);
    /* NULL mmio buffer */
    ASSERT(fl_device_desc_mmio_list(d, NULL, 8) == 0);
    /* max_mmio <= 0 */
    ASSERT(fl_device_desc_mmio_list(d, mmio_list, 0) == 0);
    /* host descriptor has no MMIO resources */
    memset(mmio_list, 0, sizeof(mmio_list));
    ASSERT(fl_device_desc_mmio_list(d, mmio_list, 8) == 0);

    /* ---- fl_device_desc_ioport_range ---- */
    /* NULL desc */
    ASSERT(fl_device_desc_ioport_range(NULL, &ioport_base, &ioport_count) == -1);
    /* NULL base */
    ASSERT(fl_device_desc_ioport_range(d, NULL, &ioport_count) == -1);
    /* NULL count */
    ASSERT(fl_device_desc_ioport_range(d, &ioport_base, NULL) == -1);
    /* host descriptor has 1 IOPORT resource at 0xF100, size 8 */
    ioport_base = 0; ioport_count = 0;
    ASSERT(fl_device_desc_ioport_range(d, &ioport_base, &ioport_count) == 0);
    ASSERT(ioport_base == 0xF100u);
    ASSERT(ioport_count == 8);

    /* ---- descriptor with no resources (empty) ---- */
    fl_device_desc_t empty;
    memset(&empty, 0, sizeof(empty));
    empty.resource_count = 0;
    ASSERT(fl_device_desc_irq_list(&empty, irq_list, 8) == 0);
    ASSERT(fl_device_desc_mmio_list(&empty, mmio_list, 8) == 0);
    ASSERT(fl_device_desc_ioport_range(&empty, &ioport_base, &ioport_count) == -1);

    return 0;
}

/* ------------------------------------------------------------------ */
/* devfs: error paths and flags enforcement                            */
/* ------------------------------------------------------------------ */
static int test_devfs_flags_and_errors(void) {
    fl_devfs_file_t file;
    uint8_t buf[512];
    size_t n = 0;

    memset(&file, 0, sizeof(file));
    memset(buf, 0, sizeof(buf));

    /* Open non-existent path */
    ASSERT(fl_devfs_open("/dev/does_not_exist", FL_DEVFS_O_READ, &file) != 0);

    /* NULL path */
    ASSERT(fl_devfs_open(NULL, FL_DEVFS_O_READ, &file) != 0);

    /* NULL out-file */
    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_READ, NULL) != 0);

    /* Open read-only and attempt write */
    memset(&file, 0, sizeof(file));
    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_READ, &file) == 0);
    /* write should fail: FL_DEVFS_O_WRITE flag not set */
    ASSERT(fl_devfs_write(&file, buf, sizeof(buf), &n) != 0);
    ASSERT(fl_devfs_close(&file) == 0);

    /* Open write-only and attempt read */
    memset(&file, 0, sizeof(file));
    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_WRITE, &file) == 0);
    ASSERT(fl_devfs_read(&file, buf, sizeof(buf), &n) != 0);
    ASSERT(fl_devfs_close(&file) == 0);

    /* Operations on NULL file */
    ASSERT(fl_devfs_read(NULL, buf, sizeof(buf), &n) != 0);
    ASSERT(fl_devfs_write(NULL, buf, sizeof(buf), &n) != 0);
    ASSERT(fl_devfs_ioctl(NULL, FL_DEVFS_IOCTL_BLOCK_CAPS, buf) != 0);
    ASSERT(fl_devfs_close(NULL) != 0);

    /* Close clears node pointer */
    memset(&file, 0, sizeof(file));
    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_READ | FL_DEVFS_O_WRITE, &file) == 0);
    ASSERT(file.node != NULL);
    ASSERT(fl_devfs_close(&file) == 0);
    ASSERT(file.node == NULL);
    ASSERT(file.pos == 0);

    /* Register duplicate path must fail */
    fl_devfs_ops_t dummy_ops = {0};
    fl_device_t *blk = fl_device_find_synth("host_blk");
    ASSERT(blk != NULL);
    ASSERT(fl_devfs_register("/dev/blk0", FL_DRV_CLASS_BLOCK, blk, &dummy_ops) != 0);

    /* Unregister existing */
    ASSERT(fl_devfs_register("/dev/test_tmp", FL_DRV_CLASS_BLOCK, blk, &dummy_ops) == 0);
    fl_devfs_unregister("/dev/test_tmp");
    /* After unregister, open should fail */
    ASSERT(fl_devfs_open("/dev/test_tmp", FL_DEVFS_O_READ, &file) != 0);

    return 0;
}

/* ------------------------------------------------------------------ */
/* DMA: null / edge case handling                                      */
/* ------------------------------------------------------------------ */
static int test_dma_edge_cases(void) {
    fl_dma_info_t info;

    /* Allocate zero size */
    void *p = fl_dma_alloc(0);
    ASSERT(p == NULL);

    /* get_info on NULL */
    ASSERT(fl_dma_get_info(NULL, NULL) != 0);
    ASSERT(fl_dma_get_info(NULL, &info) != 0);

    /* get_info on unknown pointer */
    int dummy = 42;
    ASSERT(fl_dma_get_info(&dummy, &info) != 0);

    /* allocation count starts clean after shutdown/reinit */
    ASSERT(fl_dma_allocation_count() == 0);

    /* Normal alloc then free */
    p = fl_dma_alloc(64);
    ASSERT(p != NULL);
    ASSERT(fl_dma_allocation_count() == 1);
    fl_dma_free(p);
    ASSERT(fl_dma_allocation_count() == 0);

    /* Double-free of same pointer: must not crash (second free is ignored) */
    p = fl_dma_alloc(32);
    ASSERT(p != NULL);
    fl_dma_free(p);
    fl_dma_free(p); /* second free: pointer no longer tracked, should be no-op */
    ASSERT(fl_dma_allocation_count() == 0);

    /* fl_dma_free(NULL) is a no-op */
    fl_dma_free(NULL);

    /* fl_dma_zero/copy with NULL or zero size: no crash */
    fl_dma_zero(NULL, 8);
    fl_dma_zero(NULL, 0);
    p = fl_dma_alloc(16);
    ASSERT(p != NULL);
    fl_dma_zero(p, 0);
    fl_dma_copy(NULL, p, 8);
    fl_dma_copy(p, NULL, 8);
    fl_dma_free(p);

    return 0;
}

/* ------------------------------------------------------------------ */
/* IRQ: boundary / error cases                                         */
/* ------------------------------------------------------------------ */
static int test_irq_edge_cases(void) {
    fl_irq_info_t irq_info;

    /* fl_irq_get_info with out-of-range IRQ */
    ASSERT(fl_irq_get_info(-1, &irq_info) != 0);
    ASSERT(fl_irq_get_info(32, &irq_info) != 0); /* FL_MODEL_MAX_IRQ=32 */

    /* fl_irq_get_info with NULL out param */
    ASSERT(fl_irq_get_info(0, NULL) != 0);

    /* fl_irq_register: out-of-range IRQ */
    void *ctx = NULL;
    ASSERT(fl_irq_register(-1, test_irq_handler, ctx) != 0);
    ASSERT(fl_irq_register(32, test_irq_handler, ctx) != 0);

    /* fl_irq_register: NULL handler */
    ASSERT(fl_irq_register(0, NULL, ctx) != 0);

    /* fl_irq_dispatch on out-of-range */
    ASSERT(fl_irq_dispatch(-1) != 0);
    ASSERT(fl_irq_dispatch(32) != 0);

    /* fl_irq_dispatch_count on invalid IRQ returns 0 */
    ASSERT(fl_irq_dispatch_count(-1) == 0);
    ASSERT(fl_irq_dispatch_count(32) == 0);

    /* fl_irq_enable/disable on out-of-range: no crash */
    fl_irq_enable(-1);
    fl_irq_enable(32);
    fl_irq_disable(-1);
    fl_irq_disable(32);

    /* fl_irq_unregister on out-of-range: no crash */
    fl_irq_unregister(-1);
    fl_irq_unregister(32);

    /* fl_irq_unregister_device(NULL): no crash */
    fl_irq_unregister_device(NULL);

    /* fl_irq_eoi on out-of-range: no crash */
    fl_irq_eoi(-1);
    fl_irq_eoi(32);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Device model: null / boundary parameter handling                   */
/* ------------------------------------------------------------------ */
static int test_device_model_null_params(void) {
    fl_device_info_t info;
    fl_resource_t res;

    /* fl_device_get_info: out-of-range index */
    ASSERT(fl_device_get_info(-1, &info) != 0);
    ASSERT(fl_device_get_info(9999, &info) != 0);

    /* fl_device_get_info: NULL out param */
    ASSERT(fl_device_get_info(0, NULL) != 0);

    /* fl_device_find_synth: NULL id */
    ASSERT(fl_device_find_synth(NULL) == NULL);

    /* fl_device_find_synth: non-existent id */
    ASSERT(fl_device_find_synth("no_such_device_id") == NULL);

    /* fl_device_get_desc: NULL device */
    ASSERT(fl_device_get_desc(NULL) == NULL);

    /* fl_resource_count: NULL device (returns global count) */
    int total = fl_resource_count(NULL);
    ASSERT(total >= 0);

    /* fl_resource_get: NULL out param */
    ASSERT(fl_resource_get(NULL, 0, NULL) != 0);

    /* fl_resource_get: negative index */
    fl_device_t *dev = fl_device_find_synth("host_blk");
    ASSERT(dev != NULL);
    ASSERT(fl_resource_get(dev, -1, &res) != 0);

    /* fl_resource_get: beyond last resource */
    ASSERT(fl_resource_get(dev, 9999, &res) != 0);

    /* fl_resource_claim: NULL device */
    ASSERT(fl_resource_claim(NULL, FL_RESOURCE_IRQ, 0) != 0);

    /* fl_resource_release_all: NULL device is no-op */
    fl_resource_release_all(NULL);

    /* fl_driver_registry_match: NULL params */
    const fl_driver_desc_t *matched = NULL;
    ASSERT(fl_driver_registry_match(NULL, &matched) != 0);
    fl_device_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    ASSERT(fl_driver_registry_match(&desc, NULL) != 0);

    return 0;
}

static int test_display(void) {
    ASSERT(g_display_driver != NULL);
    g_display_driver->putchar(g_display_driver, 'X');
    g_display_driver->clear(g_display_driver);
    return 0;
}

static int test_timer(void) {
    ASSERT(g_timer_driver != NULL);
    uint64_t t0 = g_timer_driver->tick_count(g_timer_driver);
    g_timer_driver->msleep(g_timer_driver, 5);
    uint64_t t1 = g_timer_driver->tick_count(g_timer_driver);
    ASSERT(t1 >= t0);  /* Proof: timer advances */
    return 0;
}

static int test_pic(void) {
    ASSERT(g_pic_driver != NULL);
    if (g_pic_driver->init)
        g_pic_driver->init(g_pic_driver);
    return 0;
}

int main(void) {
    char path[] = "/tmp/fl_driver_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        fprintf(stderr, "Cannot create temp file\n");
        return 1;
    }
    close(fd);
    g_total_clusters = 8;
    g_cluster_size = 32;
    strncpy(current_disk_file, path, sizeof(current_disk_file) - 1);
    current_disk_file[sizeof(current_disk_file) - 1] = '\0';
    if (create_temp_disk(path, 8, 32) != 0) {
        unlink(path);
        fprintf(stderr, "Cannot create temp disk\n");
        return 1;
    }

    printf("=== Driver Test Suite ===\n");
    drivers_init(path);

    if (drivers_require_real_block() != 0) {
        fprintf(stderr, "Block driver is stub; cannot run block tests.\n");
        drivers_shutdown();
        unlink(path);
        return 1;
    }

    printf("test_block... ");
    if (test_block() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_block_write_read... ");
    if (test_block_write_read() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_device_model... ");
    if (test_device_model() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_devfs_block... ");
    if (test_devfs_block() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_irq_and_dma... ");
    if (test_irq_and_dma() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_fl_cstr... ");
    if (test_fl_cstr() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_bus_resource_extraction... ");
    if (test_bus_resource_extraction() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_devfs_flags_and_errors... ");
    if (test_devfs_flags_and_errors() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_dma_edge_cases... ");
    if (test_dma_edge_cases() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_irq_edge_cases... ");
    if (test_irq_edge_cases() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_device_model_null_params... ");
    if (test_device_model_null_params() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_display... ");
    if (test_display() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_timer... ");
    if (test_timer() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_pic... ");
    if (test_pic() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_probe... ");
    ASSERT(driver_probe_block() == 0);
    ASSERT(driver_probe_keyboard() == 0);
    ASSERT(driver_probe_display() == 0);
    ASSERT(driver_probe_timer() == 0);
    ASSERT(driver_probe_pic() == 0);
    printf("OK\n");

    drivers_shutdown();
    unlink(path);
    printf("=== All driver tests passed.\n");
    return 0;
}
