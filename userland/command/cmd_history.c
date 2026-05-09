#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>

/**
 * Handle a history command by printing saved command history when requested.
 *
 * @param trimmed Command string to check; handled when equal to "history" or "his".
 * @returns `0` if `trimmed` is not "history" or "his"; `1` if the command was handled
 *          (history was printed or a missing-history message was reported).
 */
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
