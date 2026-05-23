#include "cmd_decl.h"
#include "disk.h"

int cmd_listclusters_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    list_clusters_contents();
    return 0;
}
