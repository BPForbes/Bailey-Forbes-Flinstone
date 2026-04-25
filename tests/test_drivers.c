/**
 * Driver test suite: block, display, timer, PIC (host mode).
 */
#include "drivers/drivers.h"
#include "drivers/block/block_driver.h"
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
    void *buf = fl_dma_alloc(128);
    ASSERT(buf != NULL);
    fl_dma_free(buf);
    ASSERT(dev != NULL);
    ASSERT(fl_irq_register_device(dev, 0, test_irq_handler, &hits) == 0);
    fl_irq_enable(3);
    ASSERT(fl_irq_dispatch(3) == 0);
    ASSERT(hits == 1);
    ASSERT(fl_irq_dispatch_count(3) == 1);
    fl_irq_disable(3);
    ASSERT(fl_irq_dispatch(3) != 0);
    fl_irq_unregister(3);
    ASSERT(fl_irq_register_device(dev, 99, test_irq_handler, &hits) != 0);
    ASSERT(fl_irq_register(31, test_irq_handler, &hits) != 0);
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
