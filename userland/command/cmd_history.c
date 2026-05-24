#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "util.h"
#include "fl/history_record.h"
#include <stdio.h>
#include <string.h>
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
    char raw[4097];
    char disp[4097];
    int idx = 1;
    while (fgets(raw, sizeof(raw), hf)) {
        raw[strcspn(raw, "\n")] = '\0';
        int rc = fl_history_record_unpack_cmd(raw, disp, sizeof disp, NULL, NULL,
                                               NULL);
        if (rc < 0) {
            printf("[%d] [unpack error]\n", idx++);
            continue;
        }
        printf("[%d] %s\n", idx++, disp);
    }
    fclose(hf);
    if (tmp[0])
        unlink(tmp);
    return 1;
}

int cmd_history_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 1;
}
