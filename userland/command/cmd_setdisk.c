#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "disk.h"
#include "mem_domain.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

/**
 * Selects and loads a disk file to be used as the current disk.
 *
 * Validates argument count, resolves and verifies the provided path against
 * the process jail, ensures the target file is accessible for reading,
 * updates the global `current_disk_file` with the resolved path, and loads
 * the disk header into memory.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector; argv[1] should be the disk file path to set.
 * @returns 0 on success; 1 on error (invalid usage, path blocked, or file access failure).
 */
int cmd_setdisk_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: setdisk <disk_file>\n");
        return 1;
    }
    char spath[CWD_MAX];
    mem_domain_zero(spath, sizeof(spath));
    resolve_path(args[1], spath, sizeof spath);
    if (cmd_jail_blocked_path("setdisk", args[1], spath))
        return 1;
    FILE *fp = fopen(spath, "r");
    if (!fp) {
        perror("Error opening disk file");
        return 1;
    }
    fclose(fp);
    strncpy(current_disk_file, spath, sizeof(current_disk_file) - 1);
    current_disk_file[sizeof(current_disk_file) - 1] = '\0';
    read_disk_header();
    return 0;
}
