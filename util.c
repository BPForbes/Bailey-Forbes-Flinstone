#include "util.h"
#include "common.h"
#include <ctype.h>
#include <string.h>

void resolve_path(const char *path, char *out, size_t outsize) {
    if (!path || !out || outsize == 0) return;
    if (path[0] == '/') {
        strncpy(out, path, outsize - 1);
        out[outsize - 1] = '\0';
        return;
    }
    char tmp[CWD_MAX + 256];
    if (path[0] == '\0' || (path[0] == '.' && (path[1] == '\0' || path[1] == '/'))) {
        strncpy(out, g_cwd, outsize - 1);
        out[outsize - 1] = '\0';
        return;
    }
    snprintf(tmp, sizeof(tmp), "%s/%s", g_cwd, path);
    strncpy(out, tmp, outsize - 1);
    out[outsize - 1] = '\0';
}

char *trim_whitespace(char *str) {
    if (!str)
        return NULL;
    while (isspace((unsigned char)*str))
        str++;
    if (!*str)
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return str;
}

void append_history(const char *cmd) {
    pthread_mutex_lock(&history_mutex);
    FILE *fp = fopen(HISTORY_FILE, "a");
    if (fp) {
        fprintf(fp, "%s\n", cmd);
        fclose(fp);
    }
    pthread_mutex_unlock(&history_mutex);
}

char *read_history_line(int index) {
    pthread_mutex_lock(&history_mutex);
    FILE *fp = fopen(HISTORY_FILE, "r");
    if (!fp) {
        pthread_mutex_unlock(&history_mutex);
        return NULL;
    }
    char line[512];
    int count = 0;
    char *selected = NULL;
    while (fgets(line, sizeof(line), fp)) {
        count++;
        if (count == index) {
            line[strcspn(line, "\n")] = '\0';
            selected = strdup(line);
            break;
        }
    }
    fclose(fp);
    pthread_mutex_unlock(&history_mutex);
    return selected;
}

char **load_history(int *count) {
    FILE *fp = fopen(HISTORY_FILE, "r");
    if (!fp) {
        *count = 0;
        return NULL;
    }
    int capacity = 100, cnt = 0;
    char **hist = malloc(sizeof(char*) * capacity);
    if (!hist)
        return NULL;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (cnt >= capacity) {
            capacity *= 2;
            hist = realloc(hist, sizeof(char*) * capacity);
        }
        hist[cnt++] = strdup(line);
    }
    fclose(fp);
    *count = cnt;
    return hist;
}

void free_history(char **history, int count) {
    for (int i = 0; i < count; i++)
        free(history[i]);
    free(history);
}
