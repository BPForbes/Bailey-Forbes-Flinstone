#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cluster.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_du_run(int argc, char **argv) {
    char **args = argv;
    if (argc >= 2 && !strcmp(args[1], "dtl")) {
        if (argc > 2) {
            for (int i = 2; i < argc; i++) {
                int c = atoi(args[i]);
                show_disk_detail_for_cluster(c);
                printf("\n\n");
            }
        } else {
            for (int i = 0; i < g_total_clusters; i++)
                show_disk_detail_for_cluster(i);
            printf("\n\n");
        }
        return 0;
    }
    FILE *fp = fopen(current_disk_file, "r");
    if (!fp) {
        printf("No disk file.\n");
        return 1;
    }
    char linebuf[512];
    if (fgets(linebuf, sizeof(linebuf), fp) == NULL) {
        printf("Disk file is empty.\n");
        fclose(fp);
        return 1;
    }
    int total = 0, used = 0, avail = 0, bad = 0;
    while (fgets(linebuf, sizeof(linebuf), fp)) {
        linebuf[strcspn(linebuf, "\n")] = '\0';
        char *colon = strchr(linebuf, ':');
        if (!colon) {
            bad++;
            total++;
            continue;
        }
        char *hexdata = colon + 1;
        while (isspace((unsigned char)*hexdata))
            hexdata++;
        size_t expected_len = g_cluster_size * 2;
        if (strlen(hexdata) != expected_len) {
            bad++;
            total++;
            continue;
        }
        int all_zero = 1;
        for (size_t i = 0; i < expected_len; i++) {
            if (hexdata[i] != '0') {
                all_zero = 0;
                break;
            }
        }
        if (all_zero)
            avail++;
        else
            used++;
        total++;
    }
    fclose(fp);
    int used_percent = (total > 0) ? (used * 100) / total : 0;
    int avail_percent = (total > 0) ? (avail * 100) / total : 0;
    int bad_percent = (total > 0) ? (bad * 100) / total : 0;
    printf("Disk usage::\n");
    printf("State Count Percent\n");
    printf("Used %d %d\n", used, used_percent);
    printf("Avail %d %d\n", avail, avail_percent);
    printf("Bad %d %d\n", bad, bad_percent);
    printf("Total number of clusters: %d\n", total);
    printf("Total number used: %d\n", used);
    if (avail == 0)
        printf("***Disk full***\n");
    return 0;
}

int cmd_du_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 1;
}
