#include "util.h"
#include "common.h"
#include "mem_asm.h"
#include "fat32_host.h"
#include "fl/history_record.h"
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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

static int history_on_fat32(void) {
    return g_disk_host_fat32 != 0 && g_fat32_host_vol.valid != 0;
}

static int history_fwrite_blob(FILE *fp, const void *data, size_t len) {
    return fwrite(data, 1, len, fp) == len ? 0 : -1;
}

/* Pre: history_mutex held. tmp_staging[0]=='\0' for host HISTORY_FILE; else staging path to unlink after fclose. */
static FILE *history_open_snapshot_locked(char tmp_staging[HISTORY_STAGING_PATH_SZ]) {
    if (tmp_staging)
        tmp_staging[0] = '\0';
    if (history_on_fat32()) {
        char tmpl[] = "/tmp/flshXXXXXX";
        int tfd = mkstemp(tmpl);
        if (tfd < 0)
            return NULL;
        close(tfd);
        if (fat32_host_file_get(HISTORY_DISK_PATH, tmpl) != 0) {
            unlink(tmpl);
            return NULL;
        }
        if (tmp_staging) {
            strncpy(tmp_staging, tmpl, (size_t)HISTORY_STAGING_PATH_SZ - 1u);
            tmp_staging[HISTORY_STAGING_PATH_SZ - 1u] = '\0';
        }
        return fopen(tmpl, "r");
    }
    return fopen(HISTORY_FILE, "r");
}

FILE *history_fopen_read(char tmp_staging[HISTORY_STAGING_PATH_SZ]) {
    if (!tmp_staging)
        return NULL;
    pthread_mutex_lock(&history_mutex);
    FILE *fp = history_open_snapshot_locked(tmp_staging);
    pthread_mutex_unlock(&history_mutex);
    return fp;
}

int delete_history_storage(void) {
    int rc = 0;
    pthread_mutex_lock(&history_mutex);
    if (history_on_fat32())
        (void)fat32_host_file_del(HISTORY_DISK_PATH);
    if (remove(HISTORY_FILE) != 0 && errno != ENOENT)
        rc = -1;
    pthread_mutex_unlock(&history_mutex);
    return rc;
}

void append_history_ex(fl_contract_surface_t surface, fl_result_t last_rc,
                       const char *cmd) {
    if (!cmd || cmd[0] == '\0')
        return;

    unsigned char rec[4096];
    size_t n = fl_history_record_pack((char *)rec, sizeof(rec),
                                       (uint8_t)FL_CONTRACT_BUNDLE_REV, surface,
                                       last_rc, cmd);
    if (n == (size_t)-1) {
        size_t clen = strlen(cmd);
        if (clen + 2u > sizeof rec)
            clen = sizeof rec - 2u;
        memcpy(rec, cmd, clen);
        rec[clen] = '\n';
        n = clen + 1u;
    }

    pthread_mutex_lock(&history_mutex);
    if (history_on_fat32()) {
        char tmpl[] = "/tmp/flhaXXXXXX";
        int tfd = mkstemp(tmpl);
        if (tfd >= 0) {
            close(tfd);
            if (fat32_host_file_get(HISTORY_DISK_PATH, tmpl) != 0) {
                tfd = open(tmpl, O_WRONLY | O_TRUNC);
                if (tfd >= 0)
                    close(tfd);
            }
            FILE *fp = fopen(tmpl, "a");
            if (fp) {
                int write_ok = (history_fwrite_blob(fp, rec, n) == 0);
                int close_ok = (fclose(fp) == 0);
                if (write_ok && close_ok) {
                    (void)fat32_host_file_put(tmpl, HISTORY_DISK_PATH);
                }
            }
            unlink(tmpl);
        }
    } else {
        FILE *fp = fopen(HISTORY_FILE, "a");
        if (fp) {
            int write_ok = (history_fwrite_blob(fp, rec, n) == 0);
            if (fclose(fp) != 0)
                write_ok = 0;
            (void)write_ok;
        }
    }
    pthread_mutex_unlock(&history_mutex);
}

char *read_history_line(int index) {
    char tmp[HISTORY_STAGING_PATH_SZ];
    tmp[0] = '\0';
    pthread_mutex_lock(&history_mutex);
    FILE *fp = history_open_snapshot_locked(tmp);
    pthread_mutex_unlock(&history_mutex);
    if (!fp)
        return NULL;
    char line[4097];
    int count = 0;
    char *selected = NULL;
    char decoded[4097];
    while (fgets(line, sizeof(line), fp)) {
        count++;
        if (count == index) {
            line[strcspn(line, "\n")] = '\0';
            if (fl_history_record_unpack_cmd(line, decoded, sizeof decoded,
                                             NULL, NULL, NULL) < 0) {
                selected = strdup("[unpack error]");
            } else {
                selected = strdup(decoded);
            }
            break;
        }
    }
    fclose(fp);
    if (tmp[0])
        unlink(tmp);
    return selected;
}

char **load_history(int *count) {
    char tmp[HISTORY_STAGING_PATH_SZ];
    tmp[0] = '\0';
    pthread_mutex_lock(&history_mutex);
    FILE *fp = history_open_snapshot_locked(tmp);
    pthread_mutex_unlock(&history_mutex);
    if (!fp) {
        *count = 0;
        return NULL;
    }
    int capacity = 100, cnt = 0;
    char **hist = malloc(sizeof(char *) * (size_t)capacity);
    if (!hist) {
        fclose(fp);
        if (tmp[0])
            unlink(tmp);
        *count = 0;
        return NULL;
    }
    char line[4097];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        char decoded[4097];
        const char *to_store = decoded;
        if (fl_history_record_unpack_cmd(line, decoded, sizeof decoded, NULL,
                                          NULL, NULL) < 0)
            to_store = "[unpack error]";
        if (cnt >= capacity) {
            if (capacity > INT_MAX / 2) {
                fclose(fp);
                if (tmp[0])
                    unlink(tmp);
                *count = cnt;
                return hist;
            }
            int new_capacity = capacity * 2;
            char **tmpa = realloc(hist, sizeof(char *) * (size_t)new_capacity);
            if (!tmpa) {
                fclose(fp);
                if (tmp[0])
                    unlink(tmp);
                *count = cnt;
                return hist;
            }
            capacity = new_capacity;
            hist = tmpa;
        }
        hist[cnt++] = strdup(to_store);
    }
    fclose(fp);
    if (tmp[0])
        unlink(tmp);
    *count = cnt;
    return hist;
}

void free_history(char **history, int count) {
    for (int i = 0; i < count; i++)
        free(history[i]);
    free(history);
}
