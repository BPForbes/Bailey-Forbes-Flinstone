#include "fl/audit_log.h"
#include "fl/contract_log_dispatch.h"
#include "fs_jail.h"
#include "mem_asm.h"
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

/* --- In-memory ring (P6-2): bulk moves use asm_mem_copy (x86-64 GAS/NASM + AArch64 GAS).
 * Storage is FL_RING_LOG_CAPACITY+1 bytes: up to FL_RING_LOG_CAPACITY bytes of line
 * text (including embedded newlines) plus a trailing NUL without indexing past the end.
 * --- */
static pthread_mutex_t s_ring_mu = PTHREAD_MUTEX_INITIALIZER;
static char s_ring_buf[FL_RING_LOG_CAPACITY + 1u];
static char s_ring_scratch[FL_RING_LOG_CAPACITY + 1u];
static size_t s_ring_len;
static unsigned s_ring_drops;

void fl_ring_log_reset(void) {
    pthread_mutex_lock(&s_ring_mu);
    s_ring_len = 0;
    s_ring_drops = 0;
    asm_mem_zero(s_ring_buf, sizeof s_ring_buf);
    asm_mem_zero(s_ring_scratch, sizeof s_ring_scratch);
    pthread_mutex_unlock(&s_ring_mu);
}

unsigned fl_ring_log_drop_count(void) {
    pthread_mutex_lock(&s_ring_mu);
    unsigned d = s_ring_drops;
    pthread_mutex_unlock(&s_ring_mu);
    return d;
}

void fl_ring_log_append_line(const char *line) {
    if (!line)
        return;
    size_t ln = strlen(line);
    if (ln == 0u)
        return;
    size_t add = ln + 1u;
    if (add > FL_RING_LOG_CAPACITY) {
        pthread_mutex_lock(&s_ring_mu);
        s_ring_drops++;
        pthread_mutex_unlock(&s_ring_mu);
        return;
    }

    pthread_mutex_lock(&s_ring_mu);
    while (s_ring_len + add > FL_RING_LOG_CAPACITY && s_ring_len > 0u) {
        const char *nl = memchr(s_ring_buf, '\n', s_ring_len);
        if (!nl) {
            s_ring_len = 0;
            asm_mem_zero(s_ring_buf, sizeof s_ring_buf);
            break;
        }
        size_t skip = (size_t)(nl - s_ring_buf) + 1u;
        size_t rest = s_ring_len - skip;
        asm_mem_copy(s_ring_scratch, s_ring_buf + skip, rest);
        asm_mem_copy(s_ring_buf, s_ring_scratch, rest);
        s_ring_len = rest;
        s_ring_buf[s_ring_len] = '\0';
        s_ring_drops++;
    }
    if (s_ring_len + add > FL_RING_LOG_CAPACITY) {
        s_ring_drops++;
        pthread_mutex_unlock(&s_ring_mu);
        return;
    }
    asm_mem_copy(s_ring_buf + s_ring_len, line, ln);
    s_ring_len += ln;
    s_ring_buf[s_ring_len++] = '\n';
    s_ring_buf[s_ring_len] = '\0';
    pthread_mutex_unlock(&s_ring_mu);
}

size_t fl_ring_log_copy_out(char *buf, size_t cap) {
    if (!buf || cap == 0u)
        return 0u;
    if (cap < 2u) {
        buf[0] = '\0';
        return 0u;
    }
    pthread_mutex_lock(&s_ring_mu);
    size_t n = s_ring_len;
    if (n >= cap)
        n = cap - 1u;
    if (n > 0u)
        asm_mem_copy(buf, s_ring_buf, n);
    buf[n] = '\0';
    pthread_mutex_unlock(&s_ring_mu);
    return n;
}

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
        if (!fp) {
            pthread_mutex_unlock(&g_audit_mutex);
            snprintf(line, sizeof line,
                     "type=audit_error path=%s rn=%zu context=fprintf audit_failed=fopen_null",
                     FL_AUDIT_REL_DEFAULT, rn);
            goto out_emit;
        }
        if (fprintf(fp, "%s\n", line) < 0) {
            fclose(fp);
            pthread_mutex_unlock(&g_audit_mutex);
            snprintf(line, sizeof line,
                     "type=audit_error path=%s rn=%zu context=fprintf audit_failed=fprintf",
                     FL_AUDIT_REL_DEFAULT, rn);
            goto out_emit;
        }
        if (fclose(fp) != 0) {
            pthread_mutex_unlock(&g_audit_mutex);
            snprintf(line, sizeof line,
                     "type=audit_error path=%s rn=%zu context=fprintf audit_failed=fclose",
                     FL_AUDIT_REL_DEFAULT, rn);
            goto out_emit;
        }
        pthread_mutex_unlock(&g_audit_mutex);
        goto out_emit;
    }

    pthread_mutex_lock(&g_audit_mutex);
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "a");
    if (!fp) {
        pthread_mutex_unlock(&g_audit_mutex);
        snprintf(line, sizeof line,
                 "type=audit_error path=%s rn=%zu context=fwrite audit_failed=fopen_null",
                 FL_AUDIT_REL_DEFAULT, rn);
        goto out_emit;
    }
    if (fwrite(rec, 1, rn, fp) != rn) {
        fclose(fp);
        pthread_mutex_unlock(&g_audit_mutex);
        snprintf(line, sizeof line,
                 "type=audit_error path=%s rn=%zu context=fwrite audit_failed=fwrite",
                 FL_AUDIT_REL_DEFAULT, rn);
        goto out_emit;
    }
    if (fclose(fp) != 0) {
        pthread_mutex_unlock(&g_audit_mutex);
        snprintf(line, sizeof line,
                 "type=audit_error path=%s rn=%zu context=fwrite audit_failed=fclose",
                 FL_AUDIT_REL_DEFAULT, rn);
        goto out_emit;
    }
    pthread_mutex_unlock(&g_audit_mutex);

out_emit: {
    fl_ring_log_append_line(line);
    fl_log_sink_t *sink = NULL;
    pthread_mutex_lock(&g_audit_mutex);
    sink = g_audit_sink;
    pthread_mutex_unlock(&g_audit_mutex);
    (void)fl_log_sink_emit_line(sink, (int)FL_LOG_INFO, FL_LOG_FACILITY_AUDIT, line);
}
}

int fl_audit_show_last_lines(int n) {
    if (n <= 0 || n > 10000)
        n = 32;

    /*
     * Snapshot size under the audit mutex (coordinates with set_sink), then
     * read without holding the lock so concurrent append paths are not stalled.
     */
    long sz = -1;
    pthread_mutex_lock(&g_audit_mutex);
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "rb");
    if (!fp) {
        int saved_errno = errno;
        pthread_mutex_unlock(&g_audit_mutex);
        printf("No audit log at %s (%s).\n", FL_AUDIT_REL_DEFAULT,
               strerror(saved_errno));
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        pthread_mutex_unlock(&g_audit_mutex);
        return -1;
    }
    sz = ftell(fp);
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
    fclose(fp);
    pthread_mutex_unlock(&g_audit_mutex);

    fp = fopen(FL_AUDIT_REL_DEFAULT, "rb");
    if (!fp)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    {
        long sz_now = ftell(fp);
        if (sz_now < 0) {
            fclose(fp);
            return -1;
        }
        sz = sz_now;
    }

    enum { AUDIT_READ_MAX = 256 * 1024 };
    enum { AUDIT_TAIL_BYTES_MAX = 2 * 1024 * 1024 };
    const long read_floor = (sz > (long)AUDIT_TAIL_BYTES_MAX) ? (sz - (long)AUDIT_TAIL_BYTES_MAX) : 0L;
    if (read_floor > 0)
        printf("(audit show: scanning last %d MiB of log for tail; file is larger)\n",
               (int)(AUDIT_TAIL_BYTES_MAX / (1024 * 1024)));

    size_t to_read = 0;
    char *buf = NULL;

    /* Seek backwards in chunks to find at least n newlines */
    long chunk_size = (sz < (long)AUDIT_READ_MAX) ? sz : (long)AUDIT_READ_MAX;
    long start_pos = sz - chunk_size;
    if (start_pos < read_floor)
        start_pos = read_floor;
    int found_newlines = 0;

    while (start_pos >= read_floor && found_newlines < n) {
        if (fseek(fp, start_pos, SEEK_SET) != 0) {
            if (buf)
                free(buf);
            fclose(fp);
            return -1;
        }

        size_t read_len = (size_t)(sz - start_pos);
        char *new_buf = (char *)realloc(buf, read_len + 1u);
        if (!new_buf) {
            if (buf)
                free(buf);
            fclose(fp);
            return -1;
        }
        buf = new_buf;

        if (fread(buf, 1, read_len, fp) != read_len) {
            free(buf);
            fclose(fp);
            return -1;
        }
        buf[read_len] = '\0';
        to_read = read_len;

        /* Count newlines in buffer */
        found_newlines = 0;
        for (size_t j = 0; j < to_read; j++) {
            if (buf[j] == '\n')
                found_newlines++;
        }

        /* If we found enough newlines or reached scan floor, break */
        if (found_newlines >= n || start_pos == read_floor)
            break;

        /* Move back another chunk */
        start_pos -= chunk_size;
        if (start_pos < read_floor)
            start_pos = read_floor;
    }

    fclose(fp);

    /* Find the position after the (found_newlines - n)th newline to start output at a line boundary */
    int skip = found_newlines - n;
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

void fl_audit_authz_event(const char *cmd_line, unsigned cmd_no, int denied) {
    /* Ring + sink only when audit is enabled (matches PR spec: no silent ring growth). */
    if (!fl_audit_env_enabled() || !cmd_line)
        return;

    char safe[512];
    sanitize_cmd_fragment(cmd_line, safe, sizeof safe);

    char line[768];
    int n = snprintf(line, sizeof line,
                     "type=authz jail=%d bundle=%u cmd_no=%u denied=%d cmd=%s",
                     fs_jail_is_active(), (unsigned)FL_CONTRACT_BUNDLE_REV, cmd_no,
                     denied ? 1 : 0, safe);
    if (n < 0 || n >= (int)sizeof line)
        snprintf(line, sizeof line, "type=authz denied=%d cmd_no=%u [TRUNCATED]",
                 denied ? 1 : 0, cmd_no);

    /* Intentional split lock: do not hold g_audit_mutex across fopen/fprintf/fclose. */
    pthread_mutex_lock(&g_audit_mutex);
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "a");
    if (fp) {
        if (fprintf(fp, "%s\n", line) < 0) {
            (void)fclose(fp);
        } else {
            (void)fclose(fp);
        }
    }
    pthread_mutex_unlock(&g_audit_mutex);

    fl_ring_log_append_line(line);

    /* Brief second lock: read sink pointer without blocking other writers on the file path. */
    fl_log_sink_t *sink = NULL;
    pthread_mutex_lock(&g_audit_mutex);
    sink = g_audit_sink;
    pthread_mutex_unlock(&g_audit_mutex);
    (void)fl_log_sink_emit_line(sink, (int)FL_LOG_INFO, FL_LOG_FACILITY_AUDIT, line);
}
