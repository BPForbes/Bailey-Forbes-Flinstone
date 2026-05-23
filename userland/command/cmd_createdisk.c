#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
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
    int cb = nibbleCount / 2;
    if (cb >= 512 && (cb % 512) == 0)
        snprintf(current_disk_file, sizeof(current_disk_file), "%s_disk.img", args[1]);
    else
        snprintf(current_disk_file, sizeof(current_disk_file), "%s_disk.dat", args[1]);
    read_disk_header();
    print_disk_formatted();
    if (argc >= 5 && (!strcmp(args[4], "-y") || !strcmp(args[4], "-Y")))
        interactive_shell();
    return 0;
}

int cmd_createdisk_batch_tokens_count(int argc, char **argv, int i) {
    int eat = 1;

    if (i + 3 >= argc)
        return 0;
    while (eat < 5 && i + eat < argc && argv[i + eat] && argv[i + eat][0] != '-')
        eat++;
    if (eat >= 5 && i + 4 < argc && argv[i + 4] &&
        (!strcmp(argv[i + 4], "-y") || !strcmp(argv[i + 4], "-n") ||
         !strcmp(argv[i + 4], "-Y") || !strcmp(argv[i + 4], "-N")))
        return 5;
    if (eat > 4)
        return 4;
    return eat;
}
