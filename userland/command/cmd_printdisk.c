#include "cmd_decl.h"
#include "disk.h"

/**
 * Execute the print-disk command.
 *
 * Calls print_disk_formatted() to display formatted disk information.
 *
 * @param argc Number of command-line arguments (ignored).
 * @param argv Argument vector (ignored).
 * @returns 0 on success.
 */
int cmd_printdisk_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_disk_formatted();
    return 0;
}
