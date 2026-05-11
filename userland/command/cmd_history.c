#include "common.h"
#include "cmd_decl.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <unistd.h>

int cmd_history_maybe(const char *trimmed) {
    if (strcmp(trimmed, "history") != 0 && strcmp(trimmed, "his") != 0)
        return 0;
    char tmp[HISTORY_STAGING_PATH_SZ];
    FILE *hf = history_fopen_read(tmp);
    if (!hf) {
        printf("No history.\n");
        return 1;
    }
    char l2[256];
    int idx = 1;
    while (fgets(l2, sizeof(l2), hf))
        printf("[%d] %s", idx++, l2);
    fclose(hf);
    if (tmp[0])
        unlink(tmp);
    return 1;
}
