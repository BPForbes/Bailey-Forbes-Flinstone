#include "cluster.h"
#include "disk.h"
#include "disk_asm.h"
#include "common.h"
#include "util.h"
#include "mem_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

char *convert_data_to_hex(const char *data, int inputIsText, int clusterSize) {
    int hexLen = clusterSize * 2;
    char *result = malloc(hexLen + 1);
    if (!result)
        return NULL;
    int dataLen = (int)strlen(data);
    if (inputIsText) {
        for (int i = 0; i < clusterSize; i++) {
            if (i < dataLen)
                sprintf(result + i * 2, "%02X", (unsigned char)data[i]);
            else
                strcpy(result + i * 2, "00");
        }
    } else {
        int provided = dataLen < hexLen ? dataLen : hexLen;
        asm_mem_copy(result, data, (size_t)provided);
        for (int i = provided; i < hexLen; i++)
            result[i] = '0';
        result[hexLen] = '\0';
    }
    result[hexLen] = '\0';
    return result;
}

void process_write_cluster(int clu, const char *data, int inputIsText) {
    char *hexData = convert_data_to_hex(data, inputIsText, g_cluster_size);
    if (!hexData)
        return;
    update_cluster_line(clu, hexData);
    free(hexData);
    printf("Wrote data to cluster %d.\n", clu);
}

void calculate_storage_breakdown_for_cluster(int clu) {
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) {
        printf("No disk file found.\n");
        return;
    }
    char line[256];
    int currentClu = -1;
    char *hexDataFound = NULL;
    while (fgets(line, sizeof(line), fp)) {
        char *trim = trim_whitespace(line);
        if (!*trim)
            continue;
        if (!strncmp(trim, "XX:", 3))
            continue;
        currentClu++;
        if (currentClu == clu) {
            char *colon = strchr(trim, ':');
            if (colon)
                hexDataFound = colon + 1;
            break;
        }
    }
    fclose(fp);
    if (!hexDataFound) {
        printf("Cluster %02X not found.\n", clu);
        return;
    }
    hexDataFound = trim_whitespace(hexDataFound);
    int expectedLen = g_cluster_size * 2;
    int hexLen = (int)strlen(hexDataFound);
    if (hexLen < expectedLen) {
        printf("Warning: Cluster data length (%d) is shorter than expected (%d).\n", hexLen, expectedLen);
    }
    unsigned char *bytes = malloc(g_cluster_size);
    if (!bytes)
        return;
    for (int i = 0; i < g_cluster_size; i++) {
        char byteStr[3] = {0};
        if (2 * i + 1 < hexLen) {
            byteStr[0] = hexDataFound[2 * i];
            byteStr[1] = hexDataFound[2 * i + 1];
        } else {
            strcpy(byteStr, "00");
        }
        bytes[i] = (unsigned char)strtol(byteStr, NULL, 16);
    }
    int onesCount[8] = {0}, zerosCount[8] = {0};
    for (int i = 0; i < g_cluster_size; i++) {
        for (int bit = 0; bit < 8; bit++) {
            int mask = 1 << (7 - bit);
            if (bytes[i] & mask)
                onesCount[bit]++;
            else
                zerosCount[bit]++;
        }
    }
    printf("\nStorage breakdown for cluster %02X:\n", clu);
    printf("Total bytes: %d, Total bits: %d\n", g_cluster_size, g_cluster_size * 8);
    for (int bit = 0; bit < 8; bit++) {
        printf("Bit position %d: ones = %d, zeros = %d\n", bit + 1, onesCount[bit], zerosCount[bit]);
    }
    free(bytes);
}

void delete_cluster(int clu) {
    if (clu < 0 || clu >= g_total_clusters) {
        printf("Cluster out of range.\n");
        return;
    }
    if (disk_asm_zero_cluster(clu) == 0)
        printf("Cluster %d deleted (zeroed).\n", clu);
    else
        printf("Failed to delete cluster %d.\n", clu);
}

void show_disk_detail_for_cluster(int clu) {
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) {
        printf("No disk file.\n");
        return;
    }
    char line[256];
    int currentClu = -1;
    char *hexDataFound = NULL;
    while (fgets(line, sizeof(line), fp)) {
        char *trim = trim_whitespace(line);
        if (!*trim)
            continue;
        if (!strncmp(trim, "XX:", 3))
            continue;
        currentClu++;
        if (currentClu == clu) {
            char *colon = strchr(trim, ':');
            if (colon)
                hexDataFound = colon + 1;
            break;
        }
    }
    fclose(fp);
    if (!hexDataFound) {
        printf("Cluster %02X not found.\n", clu);
        return;
    }
    hexDataFound = trim_whitespace(hexDataFound);
    int expectedLen = g_cluster_size * 2;
    int hexLen = (int)strlen(hexDataFound);
    if (hexLen < expectedLen) {
        printf("Warning: Cluster data length (%d) is shorter than expected (%d).\n", hexLen, expectedLen);
    }
    unsigned char *bytes = malloc(g_cluster_size);
    if (!bytes)
        return;
    for (int i = 0; i < g_cluster_size; i++) {
        char byteStr[3] = {0};
        if (2 * i + 1 < hexLen) {
            byteStr[0] = hexDataFound[2 * i];
            byteStr[1] = hexDataFound[2 * i + 1];
        } else {
            strcpy(byteStr, "00");
        }
        bytes[i] = (unsigned char)strtol(byteStr, NULL, 16);
    }
    int used = 0;
    for (int i = 0; i < g_cluster_size; i++) {
        if (bytes[i] != 0)
            used++;
    }
    double pct = ((double)used / g_cluster_size) * 100.0;
    char *asciiStr = malloc(g_cluster_size + 1);
    if (!asciiStr) {
        free(bytes);
        return;
    }
    for (int i = 0; i < g_cluster_size; i++) {
        asciiStr[i] = isprint(bytes[i]) ? bytes[i] : '.';
    }
    asciiStr[g_cluster_size] = '\0';
    printf("Cluster %02X details: %s\n", clu, hexDataFound);
    printf("ASCII: %s\n", asciiStr);
    printf("Used bytes: %d/%d (%.2f%%)\n", used, g_cluster_size, pct);
    free(bytes);
    free(asciiStr);
}

char *convert_hex_to_ascii(const char *hexData, int clusterSize) {
    char *ascii = malloc(clusterSize + 1);
    if (!ascii)
        return NULL;
    for (int i = 0; i < clusterSize; i++) {
        char byteStr[3] = {0};
        if (2 * i + 1 < (int)strlen(hexData)) {
            byteStr[0] = hexData[2 * i];
            byteStr[1] = hexData[2 * i + 1];
        } else {
            strcpy(byteStr, "00");
        }
        unsigned char byte = (unsigned char)strtol(byteStr, NULL, 16);
        ascii[i] = isprint(byte) ? byte : '.';
    }
    ascii[clusterSize] = '\0';
    return ascii;
}
