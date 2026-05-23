#include "cmd_decl.h"
#include "cmd_batch.h"
#include "disk.h"

int cmd_printdisk_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_disk_formatted();
    return 0;
}

int cmd_printdisk_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 1;
}
