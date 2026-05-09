#include "common.h"
#include "cmd_decl.h"
#include "cluster.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Append a new cluster entry to the currently selected disk file and increment the global cluster count.
 *
 * When provided with at least two arguments, argv[1] is interpreted as an input mode flag ("-t" or "-h" = text)
 * and argv[2] supplies the data to be converted to a hex cluster payload of size g_cluster_size and appended.
 * When invoked with fewer arguments, a cluster-sized hex payload of zero bytes is appended instead.
 * The cluster is written prefixed by its two-digit hex index and g_total_clusters is incremented after successful append.
 *
 * @param argc Number of arguments passed (affects whether a payload is provided).
 * @param argv Argument vector where argv[1] may select input mode and argv[2] supplies payload data.
 * @returns `0` on success, `1` on failure (maximum cluster count reached or unable to open the disk file).
 */
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
