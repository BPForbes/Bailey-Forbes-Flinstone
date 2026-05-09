#include "common.h"
#include "cmd_decl.h"
#include "disk.h"
#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_createdisk_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 4) {
        printf("Usage: createdisk <volume> <rowCount> <nibbleCount> [ -y | -n ]\n");
        return 1;
    }
    int rowCount = atoi(args[2]);
    int nibbleCount = atoi(args[3]);
    if (rowCount <= 0 || nibbleCount <= 0 || (nibbleCount % 2 != 0)) {
        printf("Error: rowCount must be positive and nibbleCount must be positive and even.\n");
        return 1;
    }
    flintstone_format_disk(args[1], rowCount, nibbleCount);
    snprintf(current_disk_file, sizeof(current_disk_file), "%s_disk.txt", args[1]);
    g_cluster_size = nibbleCount / 2;
    g_total_clusters = rowCount;
    print_disk_formatted();
    if (argc >= 5 && (!strcmp(args[4], "-y") || !strcmp(args[4], "-Y")))
        interactive_shell();
    return 0;
}
