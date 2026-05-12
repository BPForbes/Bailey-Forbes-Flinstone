#include "fl/audit_log.h"
#include "fs_jail.h"
#ifndef DISK_HOST_USE_LIBC_PREADV
#include "shell_history_asm.h"
#endif
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_audit_mutex = PTHREAD_MUTEX_INITIALIZER;
static fl_log_sink_t *g_audit_sink;

void fl_audit_set_sink(fl_log_sink_t *sink) {
    pthread_mutex_lock(&g_audit_mutex);
    g_audit_sink = sink;
    pthread_mutex_unlock(&g_audit_mutex);
}

static int fl_audit_env_enabled(void) {
    const char *ev = getenv(FL_AUDIT_ENV);
    return ev && ev[0] != '\0' && strcmp(ev, "0") != 0;
}

static void sanitize_cmd_fragment(const char *cmd_line, char *out, size_t out_cap) {
    size_t j = 0;
    if (!cmd_line || out_cap < 2u) {
        if (out_cap >= 1u)
            out[0] = '\0';
        return;
    }
    for (size_t i = 0; cmd_line[i] && j + 1u < out_cap; i++) {
        unsigned char c = (unsigned char)cmd_line[i];
        if (c < 32u || c == '"' || c == '\\')
            out[j++] = ' ';
        else
            out[j++] = (char)c;
    }
    out[j] = '\0';
}

static size_t audit_stage_line_record(char *rec, size_t rec_cap, const char *text) {
    size_t tlen = strlen(text);
#ifdef DISK_HOST_USE_LIBC_PREADV
    if (tlen + 1u > rec_cap)
        return (size_t)-1;
    memcpy(rec, text, tlen);
    rec[tlen] = '\n';
    return tlen + 1u;
#else
    return history_asm_append_record(rec, rec_cap, 0, text, tlen);
#endif
}

void fl_audit_shell_completed(const char *cmd_line, int host_exit_code) {
    if (!fl_audit_env_enabled() || !cmd_line)
        return;

    char safe[512];
    sanitize_cmd_fragment(cmd_line, safe, sizeof safe);

    fl_result_t mapped =
        (host_exit_code == 0) ? FL_RESULT_OK : FL_RESULT_ERR;

    char line[768];
    int n = snprintf(line, sizeof line,
                     "type=shell jail=%d bundle=%u host_rc=%d fl_rc=%d surface=%d cmd=%s",
                     fs_jail_is_active(), (unsigned)FL_CONTRACT_BUNDLE_REV,
                     host_exit_code, (int)mapped,
                     (int)FL_CONTRACT_SURFACE_FS_JAIL, safe);
    if (n < 0 || n >= (int)sizeof line) {
        snprintf(line, sizeof line, "type=shell [TRUNCATED] jail=%d bundle=%u",
                 fs_jail_is_active(), (unsigned)FL_CONTRACT_BUNDLE_REV);
    }

    char rec[1024];
    size_t rn = audit_stage_line_record(rec, sizeof rec, line);
    if (rn == (size_t)-1) {
        pthread_mutex_lock(&g_audit_mutex);
        FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "a");
        if (fp) {
            (void)(fprintf(fp, "%s\n", line) < 0 ? -1 : 0);
            (void)fclose(fp);
        }
        pthread_mutex_unlock(&g_audit_mutex);
        goto out_emit;
    }

    pthread_mutex_lock(&g_audit_mutex);
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "a");
    if (fp) {
        (void)(fwrite(rec, 1, rn, fp) == rn ? 0 : -1);
        (void)fclose(fp);
    }
    pthread_mutex_unlock(&g_audit_mutex);

out_emit: {
    fl_log_sink_t *sink = NULL;
    pthread_mutex_lock(&g_audit_mutex);
    sink = g_audit_sink;
    pthread_mutex_unlock(&g_audit_mutex);
    if (sink && sink->ops && sink->ops->emit)
        sink->ops->emit(sink, (int)FL_LOG_INFO, 9, line);
}
}

int fl_audit_show_last_lines(int n) {
    if (n <= 0 || n > 10000)
        n = 32;

    pthread_mutex_lock(&g_audit_mutex);
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "rb");
    if (!fp) {
        pthread_mutex_unlock(&g_audit_mutex);
        printf("No audit log at %s (%s).\n", FL_AUDIT_REL_DEFAULT, strerror(errno));
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        pthread_mutex_unlock(&g_audit_mutex);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        pthread_mutex_unlock(&g_audit_mutex);
        return -1;
    }
    if (sz == 0) {
        fclose(fp);
        pthread_mutex_unlock(&g_audit_mutex);
        printf("(audit log empty)\n");
        return 0;
    }
    enum { AUDIT_READ_MAX = 256 * 1024 };
    long off = 0;
    if (sz > (long)AUDIT_READ_MAX)
        off = sz - (long)AUDIT_READ_MAX;
    if (fseek(fp, off, SEEK_SET) != 0) {
        fclose(fp);
        pthread_mutex_unlock(&g_audit_mutex);
        return -1;
    }
    size_t to_read = (size_t)(sz - off);
    char *buf = (char *)malloc(to_read + 1u);
    if (!buf) {
        fclose(fp);
        pthread_mutex_unlock(&g_audit_mutex);
        return -1;
    }
    if (fread(buf, 1, to_read, fp) != to_read) {
        free(buf);
        fclose(fp);
        pthread_mutex_unlock(&g_audit_mutex);
        return -1;
    }
    buf[to_read] = '\0';
    fclose(fp);
    pthread_mutex_unlock(&g_audit_mutex);

    int nl = 0;
    for (size_t i = 0; i < to_read; i++) {
        if (buf[i] == '\n')
            nl++;
    }
    int skip = nl - n;
    if (skip < 0)
        skip = 0;
    size_t p = 0;
    int seen = 0;
    while (p < to_read && seen < skip) {
        if (buf[p] == '\n')
            seen++;
        p++;
    }
    fputs(buf + p, stdout);
    if (to_read > 0u && buf[to_read - 1u] != '\n')
        fputc('\n', stdout);
    free(buf);
    return 0;
}
