#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Parse command-line geometry and apply a soft (in-memory) disk initialization.
 *
 * Validates argv[1] and argv[2] as positive integers in the range 1..65535,
 * updates the global variables `g_total_clusters` and `g_cluster_size` on success,
 * and prints status, usage, or error messages to standard output.
 *
 * @param argc Number of command arguments.
 * @param argv Argument vector where argv[1] is <cluster_count> and argv[2] is <cluster_size>.
 * @returns 0 on success (globals updated), 1 on error (invalid arguments or insufficient arguments).
 */
int cmd_initdisk_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 3) {
        printf("Usage: initdisk <cluster_count> <cluster_size>\n");
        return 1;
    }
    int newCount = atoi(args[1]);
    int newSize = atoi(args[2]);
    if (newCount <= 0 || newSize <= 0 || newCount > 65535 || newSize > 65535) {
        printf("Invalid geometry.\n");
        return 1;
    }
    g_total_clusters = newCount;
    g_cluster_size = newSize;
    printf("Soft init: clusters=%d, size=%d bytes (in memory only).\n", g_total_clusters, g_cluster_size);
    return 0;
}
