#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "disk.h"
#include "common.h"
#include "fat32_host.h"
#include "util.h"
#include "mem_asm.h"
#include "disk_host_asm.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#ifndef DISK_HOST_USE_LIBC_PREADV
#include "shell_history_asm.h"
#endif

#define SHELL_HIST_LINE_PFX "#SHELL:"
#define FLINT_EMBH_MAGIC 0x31485345u /* 'ESH1' embedded shell history */
#define FLINT_EMBH_VER   1u
#define FLINT_EMBH_HDR   16u

static int disk_use_fat32_cluster_bytes(int cluster_bytes) {
    return (cluster_bytes >= 512 && (cluster_bytes % 512) == 0);
}

/* Scan legacy hex-line volume (XX: ruler + NN:hexdata lines). Any filename. */
static int disk_try_load_legacy_hex_path(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;
    char line[256];
    int count = 0, detectedSize = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *trim = trim_whitespace(line);
        if (!*trim)
            continue;
        if (trim[0] == '#')
            continue;
        if (!strncmp(trim, "XX:", 3))
            continue;
        char *colon = strchr(trim, ':');
        if (!colon)
            continue;
        char *hexData = trim_whitespace(colon + 1);
        int len = (int)strlen(hexData);
        if (len % 2 != 0)
            continue;
        detectedSize = len / 2;
        count++;
    }
    fclose(fp);
    if (count <= 0)
        return 0;
    g_total_clusters = count;
    g_cluster_size = detectedSize;
    g_disk_host_fat32 = 0;
    fat32_host_invalidate();
    return 1;
}

static void disk_decode_hex_pairs(const char *hexData, unsigned char *out, int nbytes) {
    int hexLen = (int)strlen(hexData);
    for (int i = 0; i < nbytes; i++) {
        if (2 * i + 1 < hexLen) {
            char bs[3] = { hexData[2 * i], hexData[2 * i + 1], '\0' };
            out[i] = (unsigned char)strtoul(bs, NULL, 16);
        } else
            out[i] = 0;
    }
}

static void disk_print_hex_line(int clu, const unsigned char *bytes, int nbytes) {
    printf("%02X:", clu);
    for (int j = 0; j < nbytes; j++)
        printf("%02X", bytes[j]);
    printf("\n");
}

void read_disk_header(void) {
    g_disk_host_fat32 = 0;
    fat32_host_invalidate();

    int fd = open(current_disk_file, O_RDONLY);
    if (fd < 0) {
        printf("No disk file found: %s\n", current_disk_file);
        return;
    }

    unsigned char boot[512];
    ssize_t br = disk_host_pread_vol(fd, boot, sizeof(boot), 0);
    if (br == (ssize_t)sizeof(boot) && fat32_host_probe_sector(boot)) {
        if (fat32_host_load_from_fd(fd) == 0) {
            g_disk_host_fat32 = 1;
            close(fd);
            printf("Loaded disk (FAT32 image FLINT.DAT): %s | Clusters: %d | Cluster Size: %d bytes\n",
                   current_disk_file, g_total_clusters, g_cluster_size);
            return;
        }
    }
    close(fd);

    if (disk_try_load_legacy_hex_path(current_disk_file)) {
        printf("Loaded disk (legacy hex text): %s | Clusters: %d | Cluster Size: %d bytes\n",
               current_disk_file, g_total_clusters, g_cluster_size);
        return;
    }

    printf("Unrecognized disk format (legacy hex lines with XX: ruler, or a FAT32 super-floppy "
           "image from createdisk/format): %s\n",
           current_disk_file);
}

void list_clusters_contents(void) {
    read_disk_header();
    if (g_disk_host_fat32) {
        int fd = open(current_disk_file, O_RDONLY);
        if (fd < 0) {
            printf("No disk file found.\n");
            return;
        }
        printf("\n--- Disk Contents ---\n");
        unsigned char *buf = malloc((size_t)g_cluster_size);
        if (!buf) {
            close(fd);
            return;
        }
        int len = g_cluster_size * 2;
        char *ruler = malloc((size_t)len + 1);
        if (ruler) {
            const char *digits = "0123456789ABCDEF";
            for (int i = 0; i < len; i++)
                ruler[i] = digits[i % 16];
            ruler[len] = '\0';
            printf("XX:%s\n", ruler);
            free(ruler);
        }
        for (int c = 0; c < g_total_clusters; c++) {
            uint64_t off = 0;
            if (fat32_host_shell_cluster_byte_offset(c, &off) != 0)
                continue;
            if (disk_host_pread_vol(fd, buf, (size_t)g_cluster_size, (off_t)off) != (ssize_t)g_cluster_size)
                printf("(read error cluster %02X)\n", c);
            else
                disk_print_hex_line(c, buf, g_cluster_size);
        }
        free(buf);
        close(fd);
        return;
    }

    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) {
        printf("No disk file found. Use '-f <file>' to set one.\n");
        return;
    }
    char line[256];
    printf("\n--- Disk Contents ---\n");
    while (fgets(line, sizeof(line), fp)) {
        char *trim = trim_whitespace(line);
        if (!*trim)
            continue;
        if (trim[0] == '#')
            continue;
        if (!strncmp(trim, "XX:", 3))
            continue;
        printf("%s\n", trim);
    }
    fclose(fp);
}

void print_disk_formatted(void) {
    read_disk_header();
    if (g_disk_host_fat32) {
        list_clusters_contents();
        return;
    }
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) {
        printf("No disk file found: %s\n", current_disk_file);
        return;
    }
    int len = g_cluster_size * 2;
    char *ruler = malloc(len + 1);
    if (!ruler) {
        fclose(fp);
        return;
    }
    const char *digits = "0123456789ABCDEF";
    for (int i = 0; i < len; i++)
        ruler[i] = digits[i % 16];
    ruler[len] = '\0';
    printf("XX:%s\n", ruler);
    free(ruler);
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *trim = trim_whitespace(line);
        if (!*trim)
            continue;
        if (trim[0] == '#')
            continue;
        if (!strncmp(trim, "XX:", 3))
            continue;
        printf("%s\n", trim);
    }
    fclose(fp);
}

void update_cluster_line(int clu, const char *hexData) {
    if (g_disk_host_fat32) {
        if (clu < 0 || clu >= g_total_clusters) {
            printf("Cluster index %d out of range.\n", clu);
            return;
        }
        unsigned char *bytes = malloc((size_t)g_cluster_size);
        if (!bytes) {
            printf("Out of memory updating cluster.\n");
            return;
        }
        disk_decode_hex_pairs(hexData, bytes, g_cluster_size);
        uint64_t off = 0;
        if (fat32_host_shell_cluster_byte_offset(clu, &off) != 0) {
            free(bytes);
            return;
        }
        int fd = open(current_disk_file, O_RDWR);
        if (fd < 0) {
            perror("open disk");
            free(bytes);
            return;
        }
        ssize_t w = disk_host_pwrite_vol(fd, bytes, (size_t)g_cluster_size, (off_t)off);
        fsync(fd);
        close(fd);
        free(bytes);
        if (w != (ssize_t)g_cluster_size) {
            printf("Cluster write failed (expected %d bytes).\n", g_cluster_size);
            return;
        }
        read_disk_header();
        return;
    }

    char **clusters = malloc(sizeof(char *) * (size_t)g_total_clusters);
    int i = 0;
    FILE *fp = fopen(current_disk_file, "r");
    char header[256];
    if (fp) {
        if (fgets(header, sizeof(header), fp) == NULL) { }
        char buf[256];
        while (i < g_total_clusters && fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\n")] = '\0';
            clusters[i] = strdup(buf);
            i++;
        }
        fclose(fp);
    }
    for (; i < g_total_clusters; i++) {
        int prefixLen = snprintf(NULL, 0, "%02X:", i);
        int entryLen = prefixLen + g_cluster_size * 2 + 1;
        char *entry = malloc((size_t)entryLen);
        if (!entry) {
            for (int k = 0; k < i; k++) free(clusters[k]);
            free(clusters);
            return;
        }
        snprintf(entry, (size_t)entryLen, "%02X:", i);
        memset(entry + prefixLen, '0', (size_t)(g_cluster_size * 2));
        entry[entryLen - 1] = '\0';
        clusters[i] = entry;
    }
    if (clu < 0 || clu >= g_total_clusters) {
        printf("Cluster index %d out of range.\n", clu);
        for (int k = 0; k < g_total_clusters; k++)
            free(clusters[k]);
        free(clusters);
        return;
    }
    char newLine[256];
    snprintf(newLine, sizeof(newLine), "%02X:%s", clu, hexData);
    free(clusters[clu]);
    clusters[clu] = strdup(newLine);

    char tmp_path[CWD_MAX + 8];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", current_disk_file);
    fp = fopen(tmp_path, "w");
    if (!fp) {
        printf("Unable to open disk file for writing.\n");
        for (int k = 0; k < g_total_clusters; k++)
            free(clusters[k]);
        free(clusters);
        return;
    }
    char *ruler = malloc((size_t)(g_cluster_size * 2 + 1));
    if (ruler) {
        for (int j = 0; j < g_cluster_size * 2; j++)
            ruler[j] = "0123456789ABCDEF"[j % 16];
        ruler[g_cluster_size * 2] = '\0';
        fprintf(fp, "XX:%s\n", ruler);
        free(ruler);
    }
    for (int k = 0; k < g_total_clusters; k++) {
        fprintf(fp, "%s\n", clusters[k]);
        free(clusters[k]);
    }
    free(clusters);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    if (rename(tmp_path, current_disk_file) != 0) {
        remove(tmp_path);
        return;
    }
    read_disk_header();
}

static void flintstone_format_txt(const char *diskFileName, const char *volumeName, int rowCount,
                                   int clusterSize) {
    FILE *fp = fopen(diskFileName, "w");
    if (!fp) {
        perror("Error creating disk file");
        exit(1);
    }
    int rulerLen = clusterSize * 2;
    char *ruler = malloc((size_t)rulerLen + 1);
    if (!ruler) {
        perror("malloc failed");
        exit(1);
    }
    const char *digits = "0123456789ABCDEF";
    for (int i = 0; i < rulerLen; i++)
        ruler[i] = digits[i % 16];
    ruler[rulerLen] = '\0';
    fprintf(fp, "XX:%s\n", ruler);
    free(ruler);
    unsigned char *clusterData = malloc((size_t)clusterSize);
    if (!clusterData) {
        perror("malloc failed");
        exit(1);
    }
    for (int i = 0; i < clusterSize; i++)
        clusterData[i] = (unsigned char)(rand() % 256);
    clusterData[0] = (rowCount > 1) ? 1 : 0;
    int volNameLen = (int)strlen(volumeName);
    int copyLen = (volNameLen < (clusterSize - 1)) ? volNameLen : (clusterSize - 1);
    asm_mem_copy(clusterData + 1, volumeName, (size_t)copyLen);
    char *hexStr = malloc((size_t)clusterSize * 2 + 1);
    if (!hexStr) {
        perror("malloc failed");
        exit(1);
    }
    for (int i = 0; i < clusterSize; i++)
        sprintf(hexStr + i * 2, "%02X", clusterData[i]);
    fprintf(fp, "00:%s\n", hexStr);
    free(hexStr);
    free(clusterData);
    for (int i = 1; i < rowCount; i++) {
        unsigned char *data = malloc((size_t)clusterSize);
        if (!data) {
            perror("malloc failed");
            exit(1);
        }
        for (int j = 0; j < clusterSize; j++)
            data[j] = (unsigned char)(rand() % 256);
        data[0] = (i < rowCount - 1) ? i + 1 : 0;
        hexStr = malloc((size_t)clusterSize * 2 + 1);
        if (!hexStr) {
            perror("malloc failed");
            exit(1);
        }
        for (int j = 0; j < clusterSize; j++)
            sprintf(hexStr + j * 2, "%02X", data[j]);
        fprintf(fp, "%02X:%s\n", i, hexStr);
        free(hexStr);
        free(data);
    }
    fclose(fp);
    g_cluster_size = clusterSize;
    g_total_clusters = rowCount;
    g_disk_host_fat32 = 0;
    fat32_host_invalidate();
    printf("Formatted disk created: %s\n", diskFileName);
}

void flintstone_format_disk(const char *volumeName, int rowCount, int nibbleCount) {
    if (nibbleCount % 2 != 0) {
        fprintf(stderr, "Error: nibble count must be even.\n");
        exit(1);
    }
    int clusterSize = nibbleCount / 2;
    char diskFileName[256];
    if (disk_use_fat32_cluster_bytes(clusterSize)) {
        snprintf(diskFileName, sizeof(diskFileName), "%s_disk.img", volumeName);
        if (fat32_host_format_image(diskFileName, volumeName, rowCount, clusterSize, volumeName) != 0) {
            fprintf(stderr, "Error: could not create FAT32 image %s\n", diskFileName);
            exit(1);
        }
        int fd = open(diskFileName, O_RDONLY);
        g_disk_host_fat32 = 0;
        fat32_host_invalidate();
        if (fd < 0 || fat32_host_load_from_fd(fd) != 0) {
            if (fd >= 0)
                close(fd);
            fprintf(stderr, "Error: FAT32 image created but volume could not be loaded: %s\n", diskFileName);
            exit(1);
        }
        close(fd);
        g_disk_host_fat32 = 1;
        printf("Formatted FAT32 disk created: %s\n", diskFileName);
    } else {
        snprintf(diskFileName, sizeof(diskFileName), "%s_disk", volumeName);
        flintstone_format_txt(diskFileName, volumeName, rowCount, clusterSize);
    }
}

void format_disk_file(const char *diskFileName, const char *volumeName, int rowCount, int nibbleCount) {
    if (nibbleCount % 2 != 0) {
        fprintf(stderr, "Error: nibble count must be even.\n");
        exit(1);
    }
    int clusterSize = nibbleCount / 2;
    if (!disk_use_fat32_cluster_bytes(clusterSize)) {
        flintstone_format_txt(diskFileName, volumeName, rowCount, clusterSize);
        return;
    }
    if (fat32_host_format_image(diskFileName, volumeName, rowCount, clusterSize, volumeName) != 0) {
        fprintf(stderr, "Error: could not create FAT32 image %s\n", diskFileName);
        exit(1);
    }
    int fd = open(diskFileName, O_RDONLY);
    g_disk_host_fat32 = 0;
    fat32_host_invalidate();
    if (fd < 0 || fat32_host_load_from_fd(fd) != 0) {
        if (fd >= 0)
            close(fd);
        fprintf(stderr, "Error: FAT32 image created but volume could not be loaded: %s\n", diskFileName);
        exit(1);
    }
    close(fd);
    g_disk_host_fat32 = 1;
    printf("Formatted FAT32 disk created: %s\n", diskFileName);
}

void disk_ensure_default_fat32(const char *path, int clusters, int bytes_per_cluster) {
    if (access(path, F_OK) == 0)
        return;
    if (!disk_use_fat32_cluster_bytes(bytes_per_cluster))
        return;
    if (fat32_host_format_image(path, "NO NAME", clusters, bytes_per_cluster, "DFLT") != 0)
        return;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        g_disk_host_fat32 = 0;
        fat32_host_invalidate();
        return;
    }
    if (fat32_host_load_from_fd(fd) != 0) {
        close(fd);
        g_disk_host_fat32 = 0;
        fat32_host_invalidate();
        return;
    }
    close(fd);
    g_disk_host_fat32 = 1;
}

/* ---------------------------------------------------------------------------
 * Embedded shell command history (no separate shell_history.txt / SH_HIST.TXT).
 * FAT32: tail of FLINT.DAT. Legacy hex: "#SHELL:<cmd>" lines after cluster rows.
 * -------------------------------------------------------------------------*/

static void shell_hist_sanitize_cmd(char *dst, size_t dstsz, const char *cmd) {
    size_t j = 0;
    if (!dst || dstsz == 0)
        return;
    for (size_t i = 0; cmd[i] && j + 1 < dstsz; i++) {
        unsigned char c = (unsigned char)cmd[i];
        if (c == '\n' || c == '\r')
            dst[j++] = ' ';
        else
            dst[j++] = (char)c;
    }
    dst[j] = '\0';
}

static size_t flint_embed_tail_bytes(void) {
    if (g_total_clusters <= 0 || g_cluster_size <= 0)
        return 0;
    uint64_t flint = (uint64_t)g_total_clusters * (uint64_t)g_cluster_size;
    if (flint < (uint64_t)FLINT_EMBH_HDR + 128u)
        return 0;
    size_t want = 65536u;
    if ((uint64_t)want > flint / 2u)
        want = (size_t)(flint / 2u);
    if (want < (size_t)FLINT_EMBH_HDR + 128u)
        return 0;
    return want;
}

static int flint_embed_region(uint64_t *off_out, size_t *len_out) {
    if (!g_disk_host_fat32 || !g_fat32_host_vol.valid)
        return -1;
    size_t tail = flint_embed_tail_bytes();
    if (tail == 0)
        return -1;
    uint64_t b0 = 0;
    if (fat32_host_shell_cluster_byte_offset(0, &b0) != 0)
        return -1;
    uint64_t flintz = (uint64_t)g_total_clusters * (uint64_t)g_cluster_size;
    if (flintz < tail)
        return -1;
    *off_out = b0 + flintz - (uint64_t)tail;
    *len_out = tail;
    return 0;
}

static uint32_t rd_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static int flint_embed_append(const char *cmd) {
    uint64_t reg_off = 0;
    size_t reglen = 0;
    if (flint_embed_region(&reg_off, &reglen) != 0)
        return -1;
    unsigned char *buf = (unsigned char *)malloc(reglen);
    if (!buf)
        return -1;
    int fd = open(current_disk_file, O_RDWR);
    if (fd < 0) {
        free(buf);
        return -1;
    }
    if (disk_host_pread_vol(fd, buf, reglen, (off_t)reg_off) != (ssize_t)reglen) {
        close(fd);
        free(buf);
        return -1;
    }
    size_t cap = reglen - (size_t)FLINT_EMBH_HDR;
    uint32_t mag = rd_le32(buf);
    uint32_t ver = rd_le32(buf + 4);
    uint32_t used = rd_le32(buf + 8);
    if (mag != FLINT_EMBH_MAGIC || ver != FLINT_EMBH_VER || used > cap) {
        memset(buf, 0, reglen);
        wr_le32(buf, FLINT_EMBH_MAGIC);
        wr_le32(buf + 4, FLINT_EMBH_VER);
        wr_le32(buf + 8, 0u);
        wr_le32(buf + 12, 0u);
        used = 0;
    }
    char safe[1024];
    shell_hist_sanitize_cmd(safe, sizeof(safe), cmd);
    unsigned char rec[4096];
    size_t recn;
#ifdef DISK_HOST_USE_LIBC_PREADV
    recn = strlen(safe);
    if (recn == 0) {
        close(fd);
        free(buf);
        return 0;
    }
    if (recn + 1 > sizeof(rec)) {
        close(fd);
        free(buf);
        return -1;
    }
    memcpy(rec, safe, recn);
    rec[recn++] = (unsigned char)'\n';
#else
    recn = history_asm_append_record((char *)rec, sizeof(rec), 0, safe, strlen(safe));
    if (recn == (size_t)-1) {
        close(fd);
        free(buf);
        return -1;
    }
    if (recn == 0) {
        close(fd);
        free(buf);
        return 0;
    }
#endif
    unsigned char *payload = buf + FLINT_EMBH_HDR;
    while (used + recn > cap) {
        size_t k = 0;
        while (k < used && payload[k] != '\n')
            k++;
        if (k < used)
            k++;
        if (k == 0)
            break;
        memmove(payload, payload + k, used - k);
        used -= (uint32_t)k;
    }
    memcpy(payload + used, rec, recn);
    used += (uint32_t)recn;
    wr_le32(buf + 8, used);
    ssize_t w = disk_host_pwrite_vol(fd, buf, reglen, (off_t)reg_off);
    fsync(fd);
    close(fd);
    free(buf);
    return w == (ssize_t)reglen ? 0 : -1;
}

static char *flint_embed_read_all(void) {
    uint64_t reg_off = 0;
    size_t reglen = 0;
    if (flint_embed_region(&reg_off, &reglen) != 0)
        return NULL;
    unsigned char *buf = (unsigned char *)malloc(reglen);
    if (!buf)
        return NULL;
    int fd = open(current_disk_file, O_RDONLY);
    if (fd < 0) {
        free(buf);
        return NULL;
    }
    if (disk_host_pread_vol(fd, buf, reglen, (off_t)reg_off) != (ssize_t)reglen) {
        close(fd);
        free(buf);
        return NULL;
    }
    close(fd);
    uint32_t mag = rd_le32(buf);
    uint32_t used = rd_le32(buf + 8);
    size_t cap = reglen - (size_t)FLINT_EMBH_HDR;
    if (mag != FLINT_EMBH_MAGIC || used > cap) {
        free(buf);
        return strdup("");
    }
    char *out = (char *)malloc((size_t)used + 1u);
    if (!out) {
        free(buf);
        return NULL;
    }
    memcpy(out, buf + FLINT_EMBH_HDR, used);
    out[used] = '\0';
    free(buf);
    return out;
}

static void flint_embed_clear(void) {
    uint64_t reg_off = 0;
    size_t reglen = 0;
    if (flint_embed_region(&reg_off, &reglen) != 0)
        return;
    unsigned char *z = (unsigned char *)calloc(1, reglen);
    if (!z)
        return;
    wr_le32(z, FLINT_EMBH_MAGIC);
    wr_le32(z + 4, FLINT_EMBH_VER);
    int fd = open(current_disk_file, O_RDWR);
    if (fd >= 0) {
        (void)disk_host_pwrite_vol(fd, z, reglen, (off_t)reg_off);
        fsync(fd);
        close(fd);
    }
    free(z);
}

static int legacy_embed_append(const char *cmd) {
    if (!current_disk_file[0])
        return -1;
    FILE *fp = fopen(current_disk_file, "a");
    if (!fp)
        return -1;
    char safe[1024];
    shell_hist_sanitize_cmd(safe, sizeof(safe), cmd);
    fprintf(fp, "%s%s\n", SHELL_HIST_LINE_PFX, safe);
    fclose(fp);
    return 0;
}

static char *legacy_embed_read_all(void) {
    if (!current_disk_file[0])
        return NULL;
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp)
        return NULL;
    size_t cap = 256, len = 0;
    char *blob = (char *)malloc(cap);
    if (!blob) {
        fclose(fp);
        return NULL;
    }
    blob[0] = '\0';
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char *trim = trim_whitespace(line);
        if (strncmp(trim, SHELL_HIST_LINE_PFX, strlen(SHELL_HIST_LINE_PFX)) != 0)
            continue;
        const char *payload = trim + strlen(SHELL_HIST_LINE_PFX);
        size_t pl = strlen(payload);
        while (len + pl + 2 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(blob, cap);
            if (!nb) {
                fclose(fp);
                free(blob);
                return NULL;
            }
            blob = nb;
        }
        if (len > 0)
            blob[len++] = '\n';
        memcpy(blob + len, payload, pl);
        len += pl;
        blob[len] = '\0';
    }
    fclose(fp);
    return blob;
}

static void legacy_embed_clear(void) {
    if (!current_disk_file[0])
        return;
    FILE *in = fopen(current_disk_file, "r");
    if (!in)
        return;
    char tmp_path[CWD_MAX + 16];
    snprintf(tmp_path, sizeof(tmp_path), "%s.hnew", current_disk_file);
    FILE *out = fopen(tmp_path, "w");
    if (!out) {
        fclose(in);
        return;
    }
    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        char *trim = trim_whitespace(line);
        if (strncmp(trim, SHELL_HIST_LINE_PFX, strlen(SHELL_HIST_LINE_PFX)) == 0)
            continue;
        fputs(line, out);
    }
    fclose(in);
    fflush(out);
    fsync(fileno(out));
    fclose(out);
    (void)rename(tmp_path, current_disk_file);
}

int disk_embedded_shell_history_append(const char *cmd) {
    if (!cmd || !current_disk_file[0])
        return -1;
    if (g_disk_host_fat32 && g_fat32_host_vol.valid)
        return flint_embed_append(cmd);
    return legacy_embed_append(cmd);
}

char *disk_embedded_shell_history_read_all(void) {
    if (!current_disk_file[0])
        return NULL;
    if (g_disk_host_fat32 && g_fat32_host_vol.valid)
        return flint_embed_read_all();
    return legacy_embed_read_all();
}

void disk_embedded_shell_history_clear(void) {
    if (!current_disk_file[0])
        return;
    if (g_disk_host_fat32 && g_fat32_host_vol.valid)
        flint_embed_clear();
    else
        legacy_embed_clear();
}

void disk_embedded_shell_history_print_list(void) {
    char *blob = disk_embedded_shell_history_read_all();
    if (!blob || !*blob) {
        printf("No history.\n");
        if (blob)
            free(blob);
        return;
    }
    int idx = 1;
    char *save = NULL;
    char *work = strdup(blob);
    free(blob);
    if (!work) {
        printf("No history.\n");
        return;
    }
    for (char *ln = strtok_r(work, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save))
        printf("[%d] %s\n", idx++, ln);
    free(work);
}
