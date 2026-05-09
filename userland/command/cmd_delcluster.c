#include "cmd_decl.h"
#include "cluster.h"
#include <stdio.h>
#include <stdlib.h>

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
