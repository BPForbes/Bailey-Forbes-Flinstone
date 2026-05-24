#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "disk.h"
#include "fat32_host.h"
#include "interpreter.h"
#include "util.h"
#include <stdio.h>

int cmd_diskput_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: diskput <host_file> [name_on_disk]\n");
        printf("       Copies any file into the FAT32 root of the current disk image.\n");
        printf("       name_on_disk is optional (defaults from basename); long names use NAME~N.EXT.\n");
        return 1;
    }
    char src[CWD_MAX];
    resolve_path(args[1], src, sizeof(src));
    if (cmd_jail_blocked_path("diskput", args[1], src))
        return 1;
    read_disk_header();
    if (!g_disk_host_fat32) {
        printf("diskput: only supported on FAT32 disk images (use setdisk with .img / large clusters).\n");
        printf("        Legacy hex volumes cannot store arbitrary binary files; use format with nibbleCount>=1024.\n");
        return 1;
    }
    const char *on_disk = (argc >= 3) ? args[2] : NULL;
    if (fat32_host_file_put(src, on_disk) != 0) {
        printf("diskput: failed (out of space, root directory full, or invalid name).\n");
        return 1;
    }
    printf("diskput: stored %s on volume.\n", src);
    return 0;
}

int cmd_diskput_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 2 < argc && argv[i + 2] && argv[i + 2][0] != '\0' &&
        argv[i + 2][0] != '-')
        return 3;
    return 2;
}
