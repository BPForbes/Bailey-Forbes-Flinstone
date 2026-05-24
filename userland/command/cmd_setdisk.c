#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "disk.h"
#include "mem_domain.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

int cmd_setdisk_run(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: setdisk <disk_file>\n");
        return 1;
    }
    char spath[CWD_MAX];
    mem_domain_zero(spath, sizeof(spath));
    resolve_path(argv[1], spath, sizeof(spath));
    if (cmd_jail_blocked_path("setdisk", argv[1], spath))
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

int cmd_setdisk_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-')
        return 2;
    return 1;
}
