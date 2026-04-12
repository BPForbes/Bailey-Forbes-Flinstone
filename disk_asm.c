#include "disk_asm.h"
#include "disk.h"
#include "mem_asm.h"
#include "mem_domain.h"

#ifdef DRIVERS_BAREMETAL
/* ------------------------------------------------------------------ */
/* Bare-metal paths: real hardware I/O backed by ASM                  */
/*   x86_64 — ATA PIO via ata_pio.s (rep insw / rep outsw)            */
/*   AArch64       — RAM disk via ramdisk.s (asm_mem_copy to .bss)    */
/* ------------------------------------------------------------------ */
#if defined(__x86_64__)
#include "ata_pio.h"

int disk_asm_read_cluster(int clu_index, unsigned char *buf) {
    if (clu_index < 0) return -1;
    return ata_pio_read_sector((uint32_t)clu_index, buf) == 0 ? 0 : -1;
}

int disk_asm_write_cluster(int clu_index, const unsigned char *buf) {
    if (clu_index < 0) return -1;
    return ata_pio_write_sector((uint32_t)clu_index, buf) == 0 ? 0 : -1;
}

int disk_asm_zero_cluster(int clu_index) {
    if (clu_index < 0) return -1;
    unsigned char zero_buf[512];
    asm_mem_zero(zero_buf, sizeof(zero_buf));
    return ata_pio_write_sector((uint32_t)clu_index, zero_buf) == 0 ? 0 : -1;
}

#elif defined(__i386__)
/* ATA PIO backend is AMD64-only (SysV AMD64 ABI in ata_pio.s). */
int disk_asm_read_cluster(int c, unsigned char *b)        { (void)c;(void)b; return -1; }
int disk_asm_write_cluster(int c, const unsigned char *b) { (void)c;(void)b; return -1; }
int disk_asm_zero_cluster(int c)                          { (void)c; return -1; }

#elif defined(__aarch64__)
#include "ramdisk.h"

int disk_asm_read_cluster(int clu_index, unsigned char *buf) {
    return ramdisk_read(clu_index, buf);
}

int disk_asm_write_cluster(int clu_index, const unsigned char *buf) {
    return ramdisk_write(clu_index, buf);
}

int disk_asm_zero_cluster(int clu_index) {
    unsigned char zero_buf[RAMDISK_SECTOR_SIZE];
    asm_mem_zero(zero_buf, sizeof(zero_buf));
    return ramdisk_write(clu_index, zero_buf);
}

#else
/* Unknown bare-metal arch — no-ops */
int disk_asm_read_cluster(int c, unsigned char *b)        { (void)c;(void)b; return -1; }
int disk_asm_write_cluster(int c, const unsigned char *b) { (void)c;(void)b; return -1; }
int disk_asm_zero_cluster(int c)                          { (void)c; return -1; }
#endif

#else /* HOST mode */
/* ------------------------------------------------------------------ */
/* Host path: text-format hex disk file (development / testing)       */
/* ------------------------------------------------------------------ */
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read cluster as raw bytes into buf (uses ASM for copy) */
int disk_asm_read_cluster(int clu_index, unsigned char *buf) {
    if (clu_index < 0 || clu_index >= g_total_clusters)
        return -1;
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) return -1;
    char line[512];
    int current = -1;
    char *hex_data = NULL;
    while (fgets(line, sizeof(line), fp)) {
        char *trim = trim_whitespace(line);
        if (!*trim || !strncmp(trim, "XX:", 3)) continue;
        current++;
        if (current == clu_index) {
            char *colon = strchr(trim, ':');
            if (colon) hex_data = trim_whitespace(colon + 1);
            break;
        }
    }
    fclose(fp);
    if (!hex_data) return -1;
    size_t hex_len = strlen(hex_data);
    size_t expected = (size_t)g_cluster_size * 2;
    size_t n_bytes = (expected < hex_len ? expected : hex_len) / 2;
    unsigned char tmp[512];
    if (g_cluster_size > 512) return -1;
    for (size_t i = 0; i < n_bytes; i++) {
        char byte_str[3] = { hex_data[i*2], hex_data[i*2+1], 0 };
        tmp[i] = (unsigned char)strtol(byte_str, NULL, 16);
    }
    if (n_bytes > 0)
        asm_mem_copy(buf, tmp, n_bytes);
    if (g_cluster_size > (int)(hex_len / 2))
        asm_mem_zero(buf + hex_len/2, (size_t)g_cluster_size - hex_len/2);
    return 0;
}

/* Write raw bytes to cluster (uses ASM for buffer prep) */
int disk_asm_write_cluster(int clu_index, const unsigned char *buf) {
    if (clu_index < 0 || clu_index >= g_total_clusters)
        return -1;
    char *hex_str = mem_domain_alloc(MEM_DOMAIN_FS, (size_t)g_cluster_size * 2 + 1);
    if (!hex_str) return -1;
    for (int i = 0; i < g_cluster_size; i++)
        sprintf(hex_str + i * 2, "%02X", buf[i]);
    hex_str[g_cluster_size * 2] = '\0';
    update_cluster_line(clu_index, hex_str);
    mem_domain_free(MEM_DOMAIN_FS, hex_str);
    return 0;
}

/* Zero cluster (uses ASM) */
int disk_asm_zero_cluster(int clu_index) {
    if (clu_index < 0 || clu_index >= g_total_clusters)
        return -1;
    unsigned char *buf = mem_domain_alloc(MEM_DOMAIN_FS, (size_t)g_cluster_size);
    if (!buf) return -1;
    asm_mem_zero(buf, (size_t)g_cluster_size);
    int r = disk_asm_write_cluster(clu_index, buf);
    mem_domain_free(MEM_DOMAIN_FS, buf);
    return r;
}

#endif /* DRIVERS_BAREMETAL */
