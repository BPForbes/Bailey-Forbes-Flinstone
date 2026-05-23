#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "disk.h"
#include "fat32_host.h"
#include "interpreter.h"
#include <stdio.h>

int cmd_diskfiles_run(int argc, char **argv) {
    read_disk_header();
    if (!g_disk_host_fat32) {
        printf("diskfiles: only supported on FAT32 disk images.\n");
        return 1;
    }
    const char *sub = (argc >= 2 && argv[1] && argv[1][0]) ? argv[1] : NULL;
    fat32_host_file_list(sub);
    return 0;
}

int cmd_diskfiles_batch_tokens_count(int argc, char **argv, int i) {
    return cmd_batch_opt_path_arity(argc, argv, i);
}
