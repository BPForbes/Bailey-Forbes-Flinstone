#include "common.h"
#include "cmd_decl.h"
#include "disk.h"
#include "fat32_host.h"
#include "interpreter.h"
#include <stdio.h>

int cmd_diskfiles_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    read_disk_header();
    if (!g_disk_host_fat32) {
        printf("diskfiles: only supported on FAT32 disk images.\n");
        return 1;
    }
    fat32_host_file_list();
    return 0;
}
