#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "disk.h"
#include "interpreter.h"
#include "util.h"
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
    int cb = nibbleCount / 2;
    const char *ext = (cb >= 512 && (cb % 512) == 0) ? "_disk.img" : "_disk.dat";
    char vpath[CWD_MAX];
    char dfile[CWD_MAX];
    /* The file that is checked is the file that is created: the resolved volume name plus its
     * suffix. A name that cannot be resolved completely, or whose file name does not fit, is
     * refused whether or not a jail is active, since the quarantine check judges it too. */
    if (resolve_path_checked(args[1], vpath, sizeof(vpath)) != 0 ||
        snprintf(dfile, sizeof(dfile), "%s%s", vpath, ext) >= (int)sizeof(dfile)) {
        printf("createdisk blocked (path too long to check): %s\n", args[1]);
        return 1;
    }
    if (cmd_jail_blocked_path("createdisk", dfile, dfile))
        return 1;
    format_disk_file(dfile, args[1], rowCount, nibbleCount);
    strncpy(current_disk_file, dfile, sizeof(current_disk_file) - 1);
    current_disk_file[sizeof(current_disk_file) - 1] = '\0';
    read_disk_header();
    print_disk_formatted();
    if (argc >= 5 && (!strcmp(args[4], "-y") || !strcmp(args[4], "-Y")))
        interactive_shell();
    return 0;
}

int cmd_createdisk_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 3 >= argc) {
        int remain = argc - i;
        return remain > 0 ? remain : 1;
    }
    if (i + 4 < argc && argv[i + 4] &&
        (!strcmp(argv[i + 4], "-y") || !strcmp(argv[i + 4], "-n") ||
         !strcmp(argv[i + 4], "-Y") || !strcmp(argv[i + 4], "-N")))
        return 5;
    return 4;
}
