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
        snprintf(diskFileName, sizeof(diskFileName), "%s_disk.dat", volumeName);
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
