#include "disk_asm.h"
#include "disk.h"
#include "mem_asm.h"
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
    for (size_t i = 0; i < (expected < hex_len ? expected : hex_len) / 2; i++) {
        char byte_str[3] = { hex_data[i*2], hex_data[i*2+1], 0 };
        buf[i] = (unsigned char)strtol(byte_str, NULL, 16);
    }
    if (g_cluster_size > (int)(hex_len / 2))
        asm_mem_zero(buf + hex_len/2, (size_t)g_cluster_size - hex_len/2);
    return 0;
}

/* Write raw bytes to cluster (uses ASM for buffer prep) */
int disk_asm_write_cluster(int clu_index, const unsigned char *buf) {
    if (clu_index < 0 || clu_index >= g_total_clusters)
        return -1;
    char *hex_str = malloc((size_t)g_cluster_size * 2 + 1);
    if (!hex_str) return -1;
    for (int i = 0; i < g_cluster_size; i++)
        sprintf(hex_str + i * 2, "%02X", buf[i]);
    hex_str[g_cluster_size * 2] = '\0';
    update_cluster_line(clu_index, hex_str);
    free(hex_str);
    return 0;
}

/* Zero cluster (uses ASM) */
int disk_asm_zero_cluster(int clu_index) {
    if (clu_index < 0 || clu_index >= g_total_clusters)
        return -1;
    unsigned char *buf = malloc((size_t)g_cluster_size);
    if (!buf) return -1;
    asm_mem_zero(buf, (size_t)g_cluster_size);
    int r = disk_asm_write_cluster(clu_index, buf);
    free(buf);
    return r;
}
