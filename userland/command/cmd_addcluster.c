#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cluster.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_addcluster_run(int argc, char **argv) {
    char **args = argv;
    read_disk_header();
    if (g_total_clusters >= 65535) {
        printf("Max cluster count reached.\n");
        return 1;
    }
    if (argc >= 3) {
        int inputIsText = (!strcmp(args[1], "-t") || !strcmp(args[1], "-h")) ? 1 : 0;
        FILE *fp = fopen(current_disk_file, "a");
        if (!fp) {
            perror("sc: open disk file");
            return 1;
        }
        fprintf(fp, "%02X:", g_total_clusters);
        char *hexData = convert_data_to_hex(args[2], inputIsText, g_cluster_size);
        fprintf(fp, "%s\n", hexData);
        free(hexData);
        fclose(fp);
        g_total_clusters++;
        printf("Created new cluster %d.\n", g_total_clusters - 1);
    } else {
        FILE *fp = fopen(current_disk_file, "a");
        if (!fp) {
            perror("sc: open disk file");
            return 1;
        }
        fprintf(fp, "%02X:", g_total_clusters);
        for (int i = 0; i < g_cluster_size * 2; i++)
            fputc('0', fp);
        fputc('\n', fp);
        fclose(fp);
        g_total_clusters++;
        printf("Created new cluster %d.\n", g_total_clusters - 1);
    }
    return 0;
}

int cmd_addcluster_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 2 < argc && (!strcmp(argv[i + 1], "-t") || !strcmp(argv[i + 1], "-h")))
        return 3;
    return 1;
}
