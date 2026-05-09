#include "common.h"
#include "cmd_decl.h"
#include "cluster.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_writecluster_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 4) {
        printf("Usage: writecluster <cluster_index> -t|-h <data>\n");
        return 1;
    }
    int clu = atoi(args[1]);
    int inputIsText = (!strcmp(args[2], "-t")) ? 1 : 0;
    process_write_cluster(clu, args[3], inputIsText);
    calculate_storage_breakdown_for_cluster(clu);
    return 0;
}
