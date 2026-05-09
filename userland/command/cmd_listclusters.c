#include "cmd_decl.h"
#include "disk.h"

/**
 * Execute the "list clusters" command and display cluster contents.
 *
 * @param argc Number of command-line arguments (ignored).
 * @param argv Command-line arguments (ignored).
 * @returns 0 on success.
 */
int cmd_listclusters_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    list_clusters_contents();
    return 0;
}
