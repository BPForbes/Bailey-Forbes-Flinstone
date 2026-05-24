#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "disk.h"
#include "fat32_host.h"
#include "interpreter.h"
#include <stdio.h>

int cmd_diskdel_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: diskdel <name_on_disk>\n");
        printf("       Removes a file from the FAT32 root (FLINT.DAT cannot be deleted).\n");
        return 1;
    }
    read_disk_header();
    if (!g_disk_host_fat32) {
        printf("diskdel: only supported on FAT32 disk images.\n");
        return 1;
    }
    if (fat32_host_file_del(args[1]) != 0) {
        printf("diskdel: failed (file not found or protected).\n");
        return 1;
    }
    printf("diskdel: removed %s from volume.\n", args[1]);
    return 0;
}

int cmd_diskdel_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 2;
}
