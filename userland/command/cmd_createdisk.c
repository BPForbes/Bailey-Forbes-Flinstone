#include "common.h"
#include "cmd_decl.h"
#include "disk.h"
#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Create and format a disk image based on command-line arguments.
 *
 * Parses and validates arguments, formats the specified volume, updates
 * global cluster parameters and the current disk filename, prints the
 * formatted disk state, and optionally enters the interactive shell.
 *
 * @param argc Number of command-line arguments; expected >= 4:
 *             createdisk <volume> <rowCount> <nibbleCount> [ -y | -n ].
 * @param argv Argument vector containing the command and parameters.
 *             argv[1] is the volume name, argv[2] is rowCount,
 *             argv[3] is nibbleCount, argv[4] may be '-y' or '-Y' to
 *             start the interactive shell after formatting.
 * @returns `0` on success; `1` if usage or validation fails (insufficient
 *          arguments, non-positive rowCount/nibbleCount, or odd nibbleCount).
 */
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
