/**
 * Driver test suite: block, display, timer, PIC (host mode).
 */
#include "drivers/drivers.h"
#include "drivers/block/block_driver.h"
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
    (void)t0;
    (void)t1;
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

    printf("test_block... ");
    if (test_block() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_block_write_read... ");
    if (test_block_write_read() != 0) { drivers_shutdown(); unlink(path); return 1; }
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

    drivers_shutdown();
    unlink(path);
    printf("=== All driver tests passed.\n");
    return 0;
}
