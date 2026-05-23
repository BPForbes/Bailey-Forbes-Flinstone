#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "disk.h"
#include "fat32_host.h"
#include "interpreter.h"
#include "util.h"
#include <stdio.h>

int cmd_diskget_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 3) {
        printf("Usage: diskget <name_on_disk> <host_file>\n");
        printf("       Extracts a file from the FAT32 root to a host path (binary-safe).\n");
        return 1;
    }
    char dst[CWD_MAX];
    resolve_path(args[2], dst, sizeof(dst));
    if (cmd_jail_blocked_path("diskget", args[2], dst))
        return 1;
    read_disk_header();
    if (!g_disk_host_fat32) {
        printf("diskget: only supported on FAT32 disk images.\n");
        printf("        Legacy hex volumes do not host separate binary files.\n");
        return 1;
    }
    if (fat32_host_file_get(args[1], dst) != 0) {
        printf("diskget: failed (file not found or I/O error).\n");
        return 1;
    }
    printf("diskget: wrote %s\n", dst);
    return 0;
}
