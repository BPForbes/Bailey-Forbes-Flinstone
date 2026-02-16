#include "disk.h"
#include "common.h"
#include "util.h"
#include "mem_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void read_disk_header(void) {
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) {
        printf("No disk file found: %s\n", current_disk_file);
        return;
    }
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
    if (count > 0) {
        g_total_clusters = count;
        g_cluster_size = detectedSize;
    }
    printf("Loaded disk: %s | Clusters: %d | Cluster Size: %d bytes\n",
           current_disk_file, g_total_clusters, g_cluster_size);
}

void list_clusters_contents(void) {
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) {
        printf("No disk file found. Use '-f <file>' to set one.\n");
        return;
    }
    read_disk_header();
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
    char **clusters = malloc(sizeof(char*) * g_total_clusters);
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
        char tmp[256];
        sprintf(tmp, "%02X:", i);
        char *zeros = malloc(g_cluster_size * 2 + 1);
        for (int j = 0; j < g_cluster_size * 2; j++)
            zeros[j] = '0';
        zeros[g_cluster_size * 2] = '\0';
        strcat(tmp, zeros);
        clusters[i] = strdup(tmp);
        free(zeros);
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

    /* Journaling-lite: write to .tmp then rename (atomic); avoids mid-write corruption */
    char tmp_path[80];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", current_disk_file);
    fp = fopen(tmp_path, "w");
    if (!fp) {
         printf("Unable to open disk file for writing.\n");
         for (int k = 0; k < g_total_clusters; k++)
             free(clusters[k]);
         free(clusters);
         return;
    }
    char *ruler = malloc(g_cluster_size * 2 + 1);
    if (ruler) {
         for (int j = 0; j < g_cluster_size * 2; j++)
             ruler[j] = "0123456789ABCDEF"[j % 16];
         ruler[g_cluster_size * 2] = '\0';
         fprintf(fp, "XX:%s\n", ruler);
         free(ruler);
    }
    for (int k = 0; k < g_total_clusters; k++){
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

void flintstone_format_disk(const char *volumeName, int rowCount, int nibbleCount) {
    if (nibbleCount % 2 != 0) {
        fprintf(stderr, "Error: nibble count must be even.\n");
        exit(1);
    }
    int clusterSize = nibbleCount / 2;
    char diskFileName[256];
    snprintf(diskFileName, sizeof(diskFileName), "%s_disk.txt", volumeName);
    FILE *fp = fopen(diskFileName, "w");
    if (!fp) { perror("Error creating disk file"); exit(1); }
    int rulerLen = nibbleCount;
    char *ruler = malloc(rulerLen + 1);
    if (!ruler) { perror("malloc failed"); exit(1); }
    const char *digits = "0123456789ABCDEF";
    for (int i = 0; i < rulerLen; i++)
        ruler[i] = digits[i % 16];
    ruler[rulerLen] = '\0';
    fprintf(fp, "XX:%s\n", ruler);
    free(ruler);
    unsigned char *clusterData = malloc(clusterSize);
    if (!clusterData) { perror("malloc failed"); exit(1); }
    for (int i = 0; i < clusterSize; i++)
        clusterData[i] = rand() % 256;
    clusterData[0] = (rowCount > 1) ? 1 : 0;
    int volNameLen = (int)strlen(volumeName);
    int copyLen = (volNameLen < (clusterSize - 1)) ? volNameLen : (clusterSize - 1);
    asm_mem_copy(clusterData + 1, volumeName, (size_t)copyLen);
    char *hexStr = malloc(clusterSize * 2 + 1);
    if (!hexStr) { perror("malloc failed"); exit(1); }
    for (int i = 0; i < clusterSize; i++)
        sprintf(hexStr + i * 2, "%02X", clusterData[i]);
    fprintf(fp, "00:%s\n", hexStr);
    free(hexStr);
    free(clusterData);
    for (int i = 1; i < rowCount; i++) {
        unsigned char *data = malloc(clusterSize);
        if (!data) { perror("malloc failed"); exit(1); }
        for (int j = 0; j < clusterSize; j++)
            data[j] = rand() % 256;
        data[0] = (i < rowCount - 1) ? i + 1 : 0;
        hexStr = malloc(clusterSize * 2 + 1);
        if (!hexStr) { perror("malloc failed"); exit(1); }
        for (int j = 0; j < clusterSize; j++)
            sprintf(hexStr + j * 2, "%02X", data[j]);
        fprintf(fp, "%02X:%s\n", i, hexStr);
        free(hexStr);
        free(data);
    }
    fclose(fp);
    g_cluster_size = clusterSize;
    g_total_clusters = rowCount;
    printf("Formatted disk created: %s\n", diskFileName);
}

void format_disk_file(const char *diskFileName, const char *volumeName, int rowCount, int nibbleCount) {
    if (nibbleCount % 2 != 0) {
        fprintf(stderr, "Error: nibble count must be even.\n");
        exit(1);
    }
    int clusterSize = nibbleCount / 2;
    FILE *fp = fopen(diskFileName, "w");
    if (!fp) { perror("Error creating disk file"); exit(1); }
    int rulerLen = nibbleCount;
    char *ruler = malloc(rulerLen + 1);
    if (!ruler) { perror("malloc failed"); exit(1); }
    const char *digits = "0123456789ABCDEF";
    for (int i = 0; i < rulerLen; i++)
        ruler[i] = digits[i % 16];
    ruler[rulerLen] = '\0';
    fprintf(fp, "XX:%s\n", ruler);
    free(ruler);
    unsigned char *clusterData = malloc(clusterSize);
    if (!clusterData) { perror("malloc failed"); exit(1); }
    for (int i = 0; i < clusterSize; i++)
        clusterData[i] = rand() % 256;
    clusterData[0] = (rowCount > 1) ? 1 : 0;
    int volNameLen = (int)strlen(volumeName);
    int copyLen = (volNameLen < (clusterSize - 1)) ? volNameLen : (clusterSize - 1);
    asm_mem_copy(clusterData + 1, volumeName, (size_t)copyLen);
    char *hexStr = malloc(clusterSize * 2 + 1);
    if (!hexStr) { perror("malloc failed"); exit(1); }
    for (int i = 0; i < clusterSize; i++)
        sprintf(hexStr + i * 2, "%02X", clusterData[i]);
    fprintf(fp, "00:%s\n", hexStr);
    free(hexStr);
    free(clusterData);
    for (int i = 1; i < rowCount; i++) {
        unsigned char *data = malloc(clusterSize);
        if (!data) { perror("malloc failed"); exit(1); }
        for (int j = 0; j < clusterSize; j++)
            data[j] = rand() % 256;
        data[0] = (i < rowCount - 1) ? i + 1 : 0;
        hexStr = malloc(clusterSize * 2 + 1);
        if (!hexStr) { perror("malloc failed"); exit(1); }
        for (int j = 0; j < clusterSize; j++)
            sprintf(hexStr + j * 2, "%02X", data[j]);
        fprintf(fp, "%02X:%s\n", i, hexStr);
        free(hexStr);
        free(data);
    }
    fclose(fp);
    g_cluster_size = clusterSize;
    g_total_clusters = rowCount;
    printf("Formatted disk created: %s\n", diskFileName);
}
