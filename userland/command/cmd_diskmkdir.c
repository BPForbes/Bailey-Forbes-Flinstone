#include "common.h"
#include "cmd_decl.h"
#include "disk.h"
#include "fat32_host.h"
#include "interpreter.h"
#include <stdio.h>

int cmd_diskmkdir_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: diskmkdir <path>\n");
        printf("       Creates nested FAT32 directories (mkdir -p semantics).\n");
        printf("       Example: diskmkdir DOCS/2026\n");
        return 1;
    }
    read_disk_header();
    if (!g_disk_host_fat32) {
        printf("diskmkdir: only supported on FAT32 disk images.\n");
        return 1;
    }
    if (fat32_host_dir_mk(args[1]) != 0) {
        printf("diskmkdir: failed (invalid path or I/O error).\n");
        return 1;
    }
    printf("diskmkdir: ok %s\n", args[1]);
    return 0;
}
