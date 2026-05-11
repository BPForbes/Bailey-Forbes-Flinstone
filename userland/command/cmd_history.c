#include "common.h"
#include "cmd_decl.h"
#include "disk.h"
#include <string.h>

int cmd_history_maybe(const char *trimmed) {
    if (strcmp(trimmed, "history") != 0 && strcmp(trimmed, "his") != 0)
        return 0;
    disk_embedded_shell_history_print_list();
    return 1;
}
