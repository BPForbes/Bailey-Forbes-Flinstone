#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>

int cmd_history_maybe(const char *trimmed) {
    if (strcmp(trimmed, "history") != 0 && strcmp(trimmed, "his") != 0)
        return 0;
    pthread_mutex_lock(&history_mutex);
    FILE *hf = fopen(HISTORY_FILE, "r");
    if (!hf) {
        pthread_mutex_unlock(&history_mutex);
        printf("No history.\n");
        return 1;
    }
    char l2[256];
    int idx = 1;
    while (fgets(l2, sizeof(l2), hf))
        printf("[%d] %s", idx++, l2);
    fclose(hf);
    pthread_mutex_unlock(&history_mutex);
    return 1;
}
