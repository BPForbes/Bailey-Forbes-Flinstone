#include "cmd_decl.h"
#include "disk.h"

int cmd_printdisk_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_disk_formatted();
    return 0;
}
