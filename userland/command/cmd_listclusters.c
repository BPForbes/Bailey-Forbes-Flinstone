#include "cmd_decl.h"
#include "cmd_batch.h"
#include "disk.h"

int cmd_listclusters_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    list_clusters_contents();
    return 0;
}

int cmd_listclusters_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 1;
}
