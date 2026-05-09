#include "cmd_decl.h"
#include "cluster.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Handle the `delcluster` command by deleting the cluster specified on the command line.
 *
 * Deletes the cluster whose index is provided as the first argument (argv[1]).
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings; argv[1] must be the cluster index.
 * @returns `0` on success after requesting cluster deletion, `1` if the required cluster index argument is missing.
 */
int cmd_delcluster_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: delcluster <cluster_index>\n");
        return 1;
    }
    int clu = atoi(args[1]);
    delete_cluster(clu);
    return 0;
}
