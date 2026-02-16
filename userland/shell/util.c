#include "util.h"
#include "common.h"
#include "mem_asm.h"
#include <ctype.h>
#include <string.h>

/* Resolve path with support for ., .., ./
 * Normalizes: . = current, .. = parent, ./foo = current/foo, a/b/../c = a/c
 */
void resolve_path(const char *path, char *out, size_t outsize) {
    if (!path || !out || outsize == 0) return;
    char full[CWD_MAX + 256];
    if (path[0] == '/') {
        strncpy(full, path, sizeof(full) - 1);
        full[sizeof(full) - 1] = '\0';
    } else if (path[0] == '\0' || (path[0] == '.' && path[1] == '\0')) {
        strncpy(out, g_cwd, outsize - 1);
        out[outsize - 1] = '\0';
        return;
    } else {
        snprintf(full, sizeof(full), "%s/%s", g_cwd, path);
    }
    /* Normalize: split into segments, collapse . and .. */
    char segs[64][256];
    int n = 0;
    char *p = full;
    while (*p && n < 64) {
        while (*p == '/') p++;
        if (!*p) break;
        char *seg = p;
        while (*p && *p != '/') p++;
        char c = *p;
        *p = '\0';
        if (!strcmp(seg, "..")) {
            if (n > 0) n--;
        } else if (strcmp(seg, ".")) {
            strncpy(segs[n], seg, 255);
            segs[n][255] = '\0';
            n++;
        }
        *p = c;
        if (*p) p++;
    }
    size_t len = 0;
    if (full[0] == '/' && len < outsize - 1) { out[len++] = '/'; }
    for (int i = 0; i < n && len < outsize - 1; i++) {
        if (i > 0 || len == 0) { if (len > 0) out[len++] = '/'; }
        size_t slen = strlen(segs[i]);
        if (len + slen < outsize) {
            asm_mem_copy(out + len, segs[i], slen + 1);
            len += slen;
        }
    }
    if (len == 0) { out[0] = '.'; out[1] = '\0'; }
    else out[len] = '\0';
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
