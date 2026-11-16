/*
 * Signed command log (FL-04): see cmd_journal.h for the record format, the storage and recovery
 * semantics, the bounds and the failure policy. Hosted only.
 *
 * Reuse: storage is the KeyQuorum checkpoints' table (fl_kq_storage_t); the hash is OpenSSL SHA-256
 * as elsewhere in the tree; records are built and read with net_codec.h's bounded big-endian codec
 * (fl_bytes_sticky_writer_t, fl_bytes_reader_t, the str8 helpers); the category allowlist is the
 * shell's own command registry (fl_shell_cmd_lookup) and the first word is found with
 * fl_shell_tokenize_line; random ids come from fl_random_bytes; the wall clock from
 * fl_time_wall_sec; messages go through fl_color_error / fl_color_warn_local; every copy, zeroing and
 * equality goes through the ASM-backed mem_domain_* wrappers.
 */
#include "cmd_journal.h"

#include "cmd_decl.h"
#include "fl/random.h"
#include "fl_colors.h"
#include "mem_domain.h"
#include "net_codec.h"
#include "shell_tokenize.h"
#include "timekeeping.h"

#include <openssl/sha.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CMDLOG_MAGIC "FLCL"
#define CMDLOG_PATH_MAX 256u
#define CMDLOG_FIRST_WORD_SCAN 256u /* only this much of a line is ever copied, then wiped */

struct fl_cmdlog {
    pthread_mutex_t mu;
    const fl_kq_storage_t *st;
    char dir[CMDLOG_PATH_MAX]; /* without a trailing '/' (the open-registry key) */
    const fl_cmdlog_signer_t *signer;
    fl_cmdlog_limits_t lim;
    uint8_t journal_id[FL_CMDLOG_ID_LEN];
    uint8_t device_id[FL_CMDLOG_ID_LEN];
    uint64_t next_seq;
    uint8_t last_hash[FL_CMDLOG_HASH_LEN];
    unsigned segments; /* segment files that exist; the active one is segments - 1 */
    uint8_t *seg;      /* the active segment's good bytes (capacity lim.segment_max) */
    size_t seg_len;
    uint8_t *scratch;  /* the next version of the active segment */
    size_t total;      /* bytes in every segment */
    int tail_pending;  /* the active segment on storage may end in a partial record */
    int next_dirty;    /* a failed write may have left segment index `segments` behind */
    struct fl_cmdlog *reg_next;
};

/* In-process registry of open journals: one handle per (storage table, directory). */
static pthread_mutex_t s_reg_mu = PTHREAD_MUTEX_INITIALIZER;
static fl_cmdlog_t *s_reg_head;

/* ---------------------------------------------------------------------------------------------
 * Limits, paths, reasons
 * -------------------------------------------------------------------------------------------*/

static fl_cmdlog_limits_t limits_or_default(const fl_cmdlog_limits_t *in) {
    fl_cmdlog_limits_t l = {0};

    if (in)
        l = *in;
    if (l.record_max == 0 || l.record_max > FL_CMDLOG_RECORD_MAX)
        l.record_max = FL_CMDLOG_RECORD_MAX;
    if (l.segment_max == 0)
        l.segment_max = FL_CMDLOG_SEGMENT_MAX_DEFAULT;
    if (l.segment_max < l.record_max)
        l.segment_max = l.record_max; /* one record always fits a fresh segment */
    if (l.total_max == 0)
        l.total_max = FL_CMDLOG_TOTAL_MAX_DEFAULT;
    if (l.segments_max == 0)
        l.segments_max = FL_CMDLOG_SEGMENTS_MAX_DEFAULT;
    if (l.segments_max > FL_CMDLOG_SEGMENTS_HARD_MAX)
        l.segments_max = FL_CMDLOG_SEGMENTS_HARD_MAX;
    return l;
}

static int seg_path(const char *dir, unsigned idx, char *out, size_t cap) {
    size_t d = dir ? strlen(dir) : 0;
    int n;

    if (d == 0)
        return -1;
    n = snprintf(out, cap, "%s%scmdlog-%06u.seg", dir, dir[d - 1] == '/' ? "" : "/", idx);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static int seg_exists(const fl_kq_storage_t *st, const char *dir, unsigned idx) {
    char path[CMDLOG_PATH_MAX];

    if (seg_path(dir, idx, path, sizeof path) != 0)
        return -1;
    return st->exists(st->ctx, path);
}

static void set_reason(char *reason, size_t cap, const char *text) {
    if (reason && cap)
        snprintf(reason, cap, "%s", text);
}

static int sha256(const uint8_t *p, size_t n, uint8_t out[FL_CMDLOG_HASH_LEN]) {
    return SHA256(p, n, out) ? 0 : -1;
}

/* ---------------------------------------------------------------------------------------------
 * Parsing (net_codec.h reader)
 * -------------------------------------------------------------------------------------------*/

typedef struct {
    size_t len;        /* whole record */
    size_t signed_len; /* signed bytes: [4, 4 + signed_len) */
    uint16_t version;
    char domain[FL_CMDLOG_LABEL_MAX];
    uint8_t journal_id[FL_CMDLOG_ID_LEN], device_id[FL_CMDLOG_ID_LEN];
    uint8_t prev_hash[FL_CMDLOG_HASH_LEN], event_id[FL_CMDLOG_ID_LEN], corr[FL_CMDLOG_ID_LEN];
    uint64_t seq;
    char key_id[FL_CMDLOG_LABEL_MAX];
    uint8_t type;
    uint8_t cosig;
    uint8_t sig[FL_CMDLOG_SIG_MAX];
    uint8_t sig_len;
} rec_t;

/* 0 parsed; -1 malformed; -2 incomplete (the bytes end inside a record). */
static int parse_record(const uint8_t *p, size_t avail, size_t record_max, rec_t *out) {
    fl_bytes_reader_t r;
    uint32_t len = 0, rc = 0;
    uint64_t ts = 0;
    uint8_t magic[4], trust = 0;
    char principal[FL_CMDLOG_LABEL_MAX], category[FL_CMDLOG_CATEGORY_MAX];

    mem_domain_zero(out, sizeof *out);
    if (avail < 4)
        return -2;
    len = fl_net_get_u32_be(p);
    if (len < 8 || len > record_max || len > UINT16_MAX)
        return -1;
    if (len > avail)
        return -2;
    out->len = len;
    fl_bytes_reader_init(&r, p, (uint16_t)len);
    (void)fl_bytes_reader_read_u32(&r, &rc); /* record_len, already taken */
    (void)fl_bytes_reader_read_bytes(&r, magic, sizeof magic);
    if (r.bad || !mem_domain_equal(magic, CMDLOG_MAGIC, 4))
        return -1;
    (void)fl_bytes_reader_read_u16(&r, &out->version);
    (void)fl_bytes_reader_read_str8(&r, out->domain, sizeof out->domain);
    (void)fl_bytes_reader_read_bytes(&r, out->journal_id, FL_CMDLOG_ID_LEN);
    (void)fl_bytes_reader_read_bytes(&r, out->device_id, FL_CMDLOG_ID_LEN);
    (void)fl_bytes_reader_read_u64(&r, &out->seq);
    (void)fl_bytes_reader_read_bytes(&r, out->prev_hash, FL_CMDLOG_HASH_LEN);
    (void)fl_bytes_reader_read_bytes(&r, out->event_id, FL_CMDLOG_ID_LEN);
    (void)fl_bytes_reader_read_str8(&r, out->key_id, sizeof out->key_id);
    (void)fl_bytes_reader_read_str8(&r, principal, sizeof principal);
    (void)fl_bytes_reader_read_u8(&r, &out->type);
    (void)fl_bytes_reader_read_str8(&r, category, sizeof category);
    (void)fl_bytes_reader_read_u32(&r, &rc);
    (void)fl_bytes_reader_read_u64(&r, &ts);
    (void)fl_bytes_reader_read_u8(&r, &trust);
    (void)fl_bytes_reader_read_bytes(&r, out->corr, FL_CMDLOG_ID_LEN);
    (void)fl_bytes_reader_read_u8(&r, &out->cosig);
    if (out->cosig == 1) {
        uint8_t nonce[FL_CMDLOG_ID_LEN], cosig_len = 0, cosig[FL_CMDLOG_SIG_MAX];
        char cosigner[FL_CMDLOG_LABEL_MAX];

        (void)fl_bytes_reader_read_bytes(&r, nonce, sizeof nonce);
        (void)fl_bytes_reader_read_str8(&r, cosigner, sizeof cosigner);
        if (fl_bytes_reader_read_u8(&r, &cosig_len) != FL_RESULT_OK || cosig_len == 0 ||
            cosig_len > FL_CMDLOG_SIG_MAX)
            return -1;
        (void)fl_bytes_reader_read_bytes(&r, cosig, cosig_len);
    } else if (out->cosig != 0) {
        return -1;
    }
    if (trust > FL_CMDLOG_TIME_TRUSTED)
        return -1;
    out->signed_len = (size_t)r.off - 4u;
    if (fl_bytes_reader_read_u8(&r, &out->sig_len) != FL_RESULT_OK || out->sig_len == 0 ||
        out->sig_len > FL_CMDLOG_SIG_MAX)
        return -1;
    (void)fl_bytes_reader_read_bytes(&r, out->sig, out->sig_len);
    if (r.bad || r.off != len)
        return -1; /* record_len must be exactly what the fields fill */
    return 0;
}

/* ---------------------------------------------------------------------------------------------
 * Verify / recover
 * -------------------------------------------------------------------------------------------*/

typedef struct {
    fl_cmdlog_corr_t *v;
    size_t n, cap;
} open_set_t;

static int open_add(open_set_t *s, const uint8_t id[FL_CMDLOG_ID_LEN]) {
    if (s->n == s->cap) {
        size_t cap = s->cap ? s->cap * 2u : 16u;
        fl_cmdlog_corr_t *v = (fl_cmdlog_corr_t *)mem_domain_realloc(MEM_DOMAIN_FS, s->v, cap * sizeof *v);

        if (!v)
            return -1;
        s->v = v;
        s->cap = cap;
    }
    mem_domain_copy(s->v[s->n].id, id, FL_CMDLOG_ID_LEN);
    s->n++;
    return 0;
}

static int open_close(open_set_t *s, const uint8_t id[FL_CMDLOG_ID_LEN]) {
    for (size_t i = 0; i < s->n; i++)
        if (mem_domain_equal(s->v[i].id, id, FL_CMDLOG_ID_LEN)) {
            s->v[i] = s->v[s->n - 1u];
            s->n--;
            return 1;
        }
    return 0;
}

static void free_fs(uint8_t *p) {
    if (p)
        mem_domain_free(MEM_DOMAIN_FS, p);
}

/* Checks one parsed record against the journal so far; FL_CMDLOG_V_OK or the verdict. */
static fl_cmdlog_verdict_code_t check_record(const rec_t *rec, const uint8_t *raw, const fl_cmdlog_signer_t *signer,
                                             const uint8_t *expect_device, fl_cmdlog_report_t *rep) {
    static const uint8_t zero_hash[FL_CMDLOG_HASH_LEN] = {0};

    if (rec->version != FL_CMDLOG_FORMAT_VERSION || strcmp(rec->domain, FL_CMDLOG_DOMAIN) != 0 ||
        (rec->type != FL_CMDLOG_EV_ATTEMPT && rec->type != FL_CMDLOG_EV_COMPLETION &&
         rec->type != FL_CMDLOG_EV_REFUSAL))
        return FL_CMDLOG_V_MALFORMED;
    /* One journal, one signer: a record signed by any other key the verifier knows is foreign. */
    if (strcmp(rec->key_id, signer->key_id) != 0)
        return FL_CMDLOG_V_FOREIGN;
    if (signer->verify(signer->ctx, rec->key_id, FL_CMDLOG_DOMAIN, raw + 4, rec->signed_len, rec->sig,
                       rec->sig_len) != 1)
        return FL_CMDLOG_V_BAD_SIGNATURE;
    if (rep->records == 0) {
        mem_domain_copy(rep->journal_id, rec->journal_id, FL_CMDLOG_ID_LEN);
        mem_domain_copy(rep->device_id, rec->device_id, FL_CMDLOG_ID_LEN);
    } else if (!mem_domain_equal(rep->journal_id, rec->journal_id, FL_CMDLOG_ID_LEN) ||
               !mem_domain_equal(rep->device_id, rec->device_id, FL_CMDLOG_ID_LEN)) {
        return FL_CMDLOG_V_FOREIGN;
    }
    if (expect_device && !mem_domain_equal(expect_device, rec->device_id, FL_CMDLOG_ID_LEN))
        return FL_CMDLOG_V_FOREIGN;
    if (rec->seq != rep->next_seq)
        return FL_CMDLOG_V_BAD_SEQUENCE;
    if (!mem_domain_equal(rec->prev_hash, rep->next_seq == 0 ? zero_hash : rep->last_hash, FL_CMDLOG_HASH_LEN))
        return FL_CMDLOG_V_BROKEN_CHAIN;
    if (rec->type == FL_CMDLOG_EV_ATTEMPT && !mem_domain_equal(rec->corr, rec->event_id, FL_CMDLOG_ID_LEN))
        return FL_CMDLOG_V_MALFORMED;
    return FL_CMDLOG_V_OK;
}

/*
 * Walks the journal. When keep_last is given, *keep_last receives the last segment's verified
 * bytes (MEM_DOMAIN_FS memory, free_fs) and *keep_len their length.
 */
static int walk(const fl_kq_storage_t *st, const char *dir, const fl_cmdlog_signer_t *signer,
                const fl_cmdlog_limits_t *lim, const uint8_t *expect_device, fl_cmdlog_report_t *rep,
                uint8_t **keep_last, size_t *keep_len) {
    open_set_t open = {0};
    unsigned idx;

    mem_domain_zero(rep, sizeof *rep);
    if (keep_last) {
        *keep_last = NULL;
        *keep_len = 0;
    }
    for (idx = 0; idx < lim->segments_max && rep->status == FL_CMDLOG_V_OK && !rep->tail_incomplete; idx++) {
        char path[CMDLOG_PATH_MAX];
        uint8_t *data = NULL;
        size_t len = 0, pos = 0;
        int ex, rc;

        if (seg_path(dir, idx, path, sizeof path) != 0) {
            rep->status = FL_CMDLOG_V_IO;
            break;
        }
        ex = st->exists(st->ctx, path);
        if (ex == 0)
            break;
        if (ex < 0) {
            rep->status = FL_CMDLOG_V_IO;
            break;
        }
        rc = st->read_file(st->ctx, path, lim->segment_max, &data, &len);
        if (rc != 0) {
            free_fs(data);
            rep->status = rc == -2 ? FL_CMDLOG_V_MALFORMED : FL_CMDLOG_V_IO;
            break;
        }
        rep->segments++;
        rep->total_bytes += len;
        while (pos < len) {
            rec_t rec;
            const uint8_t *p = data + pos;
            int pr = parse_record(p, len - pos, lim->record_max, &rec);
            fl_cmdlog_verdict_code_t v;

            if (pr == -2) {
                /* A torn tail, if this is the last segment; the gap probe below fails the walk if
                 * any later segment exists. */
                rep->tail_incomplete = 1;
                rep->tail_bytes = len - pos;
                break;
            }
            v = pr != 0 ? FL_CMDLOG_V_MALFORMED : check_record(&rec, p, signer, expect_device, rep);
            if (v == FL_CMDLOG_V_OK) {
                if (rec.type == FL_CMDLOG_EV_ATTEMPT) {
                    if (open_add(&open, rec.corr) != 0)
                        v = FL_CMDLOG_V_IO;
                } else if (!open_close(&open, rec.corr)) {
                    rep->orphan_closes++;
                }
            }
            if (v == FL_CMDLOG_V_OK && sha256(p, rec.len, rep->last_hash) != 0)
                v = FL_CMDLOG_V_IO;
            if (v != FL_CMDLOG_V_OK) {
                rep->status = v;
                break;
            }
            if (rec.cosig)
                rep->cosigs_unverified++;
            rep->next_seq++;
            rep->records++;
            pos += rec.len;
        }
        if (rep->status != FL_CMDLOG_V_OK) {
            rep->bad_seq = rep->next_seq;
            free_fs(data);
            break;
        }
        if (keep_last) {
            free_fs(*keep_last);
            *keep_last = data;
            *keep_len = pos; /* the verified prefix: a torn tail is left out */
        } else {
            free_fs(data);
        }
    }
    /* No segment may exist after the point where the walk stopped: a missing middle segment, a
     * partial record that is not at the very end, or a segment past the retention bound. */
    if (rep->status == FL_CMDLOG_V_OK) {
        for (unsigned k = rep->segments; k < FL_CMDLOG_SEGMENTS_HARD_MAX; k++) {
            int ex = seg_exists(st, dir, k);

            if (ex != 0) {
                rep->status = ex < 0 ? FL_CMDLOG_V_IO : FL_CMDLOG_V_GAP;
                rep->bad_seq = rep->next_seq;
                break;
            }
        }
    }
    rep->open_attempts = (unsigned)open.n;
    for (size_t i = 0; i < open.n && i < FL_CMDLOG_REPORT_OPEN_MAX; i++)
        rep->open[i] = open.v[i];
    if (open.v)
        mem_domain_free(MEM_DOMAIN_FS, open.v);
    if (rep->status != FL_CMDLOG_V_OK && keep_last) {
        free_fs(*keep_last);
        *keep_last = NULL;
        *keep_len = 0;
    }
    return rep->status == FL_CMDLOG_V_OK ? 0 : -1;
}

int fl_cmdlog_verify(const fl_kq_storage_t *storage, const char *dir, const fl_cmdlog_signer_t *signer,
                     const uint8_t *expect_device, const fl_cmdlog_limits_t *limits, fl_cmdlog_report_t *report) {
    fl_cmdlog_limits_t lim = limits_or_default(limits);

    if (!report || !storage || !dir || !signer || !signer->verify || !signer->key_id)
        return -1;
    return walk(storage, dir, signer, &lim, expect_device, report, NULL, NULL);
}

/* ---------------------------------------------------------------------------------------------
 * Open / close
 * -------------------------------------------------------------------------------------------*/

/* Adds j to the registry unless its (storage, dir) is already open. */
static int reg_add(fl_cmdlog_t *j) {
    int taken = 0;

    pthread_mutex_lock(&s_reg_mu);
    for (fl_cmdlog_t *o = s_reg_head; o; o = o->reg_next)
        if (o->st == j->st && strcmp(o->dir, j->dir) == 0)
            taken = 1;
    if (!taken) {
        j->reg_next = s_reg_head;
        s_reg_head = j;
    }
    pthread_mutex_unlock(&s_reg_mu);
    return taken ? -1 : 0;
}

static void reg_remove(fl_cmdlog_t *j) {
    pthread_mutex_lock(&s_reg_mu);
    for (fl_cmdlog_t **pp = &s_reg_head; *pp; pp = &(*pp)->reg_next)
        if (*pp == j) {
            *pp = j->reg_next;
            break;
        }
    pthread_mutex_unlock(&s_reg_mu);
}

static void destroy(fl_cmdlog_t *j) {
    pthread_mutex_destroy(&j->mu);
    if (j->seg)
        mem_domain_secure_zero(j->seg, j->lim.segment_max);
    if (j->scratch)
        mem_domain_secure_zero(j->scratch, j->lim.segment_max);
    free_fs(j->seg);
    free_fs(j->scratch);
    mem_domain_secure_zero(j, sizeof *j);
    mem_domain_free(MEM_DOMAIN_FS, j);
}

int fl_cmdlog_open(const fl_cmdlog_config_t *cfg, fl_cmdlog_t **out, fl_cmdlog_report_t *report) {
    fl_cmdlog_report_t local;
    fl_cmdlog_report_t *rep = report ? report : &local;
    fl_cmdlog_t *j;
    uint8_t *last = NULL;
    size_t last_len = 0, dlen;

    if (out)
        *out = NULL;
    if (report)
        mem_domain_zero(report, sizeof *report);
    if (!cfg || !out || !cfg->storage || !cfg->dir || !cfg->signer || !cfg->signer->sign ||
        !cfg->signer->verify || !cfg->signer->key_id || strlen(cfg->signer->key_id) == 0 ||
        strlen(cfg->signer->key_id) >= FL_CMDLOG_LABEL_MAX || strlen(cfg->dir) == 0 ||
        strlen(cfg->dir) >= CMDLOG_PATH_MAX)
        return FL_CMDLOG_OPEN_ERROR;
    j = (fl_cmdlog_t *)mem_domain_calloc(MEM_DOMAIN_FS, 1, sizeof *j);
    if (!j)
        return FL_CMDLOG_OPEN_ERROR;
    j->st = cfg->storage;
    j->lim = limits_or_default(&cfg->limits);
    j->signer = cfg->signer;
    dlen = strlen(cfg->dir);
    while (dlen > 1 && cfg->dir[dlen - 1] == '/')
        dlen--; /* "/x/" and "/x" name one journal */
    mem_domain_copy(j->dir, cfg->dir, dlen);
    j->dir[dlen] = '\0';
    mem_domain_copy(j->device_id, cfg->device_id, FL_CMDLOG_ID_LEN);
    if (pthread_mutex_init(&j->mu, NULL) != 0) {
        mem_domain_free(MEM_DOMAIN_FS, j);
        return FL_CMDLOG_OPEN_ERROR;
    }
    if (reg_add(j) != 0) {
        destroy(j);
        return FL_CMDLOG_OPEN_BUSY;
    }
    j->seg = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, j->lim.segment_max);
    j->scratch = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, j->lim.segment_max);
    if (!j->seg || !j->scratch)
        goto fail;
    /* Every record must be this device's and this signer's: a journal that does not verify, or
     * belongs to another device or key, is evidence and is never appended to. */
    if (walk(j->st, j->dir, j->signer, &j->lim, cfg->device_id, rep, &last, &last_len) != 0)
        goto fail;
    if (rep->records == 0) {
        if (fl_random_bytes(j->journal_id, FL_CMDLOG_ID_LEN) != 0)
            goto fail;
    } else {
        mem_domain_copy(j->journal_id, rep->journal_id, FL_CMDLOG_ID_LEN);
    }
    j->next_seq = rep->next_seq;
    mem_domain_copy(j->last_hash, rep->last_hash, FL_CMDLOG_HASH_LEN);
    j->segments = rep->segments;
    j->total = rep->total_bytes - rep->tail_bytes; /* the torn tail goes on the next write */
    j->tail_pending = rep->tail_incomplete;
    if (last) {
        mem_domain_copy(j->seg, last, last_len);
        j->seg_len = last_len;
        free_fs(last);
    }
    *out = j;
    return FL_CMDLOG_OPEN_OK;
fail:
    free_fs(last);
    reg_remove(j);
    destroy(j);
    return FL_CMDLOG_OPEN_ERROR;
}

void fl_cmdlog_close(fl_cmdlog_t *j) {
    if (!j)
        return;
    reg_remove(j);
    destroy(j);
}

/* ---------------------------------------------------------------------------------------------
 * Redaction
 * -------------------------------------------------------------------------------------------*/

/* The line-mode builtins the interpreter parses before argv (the cmd_decl.h *_maybe functions,
 * interpreter.c); every other known name comes from the command registry. TODO(FL-04): move these
 * names into the shared registry (userland/command/cmd_registry.c, another team's file today) so
 * there is one list. */
static const char *const k_line_builtins[] = {"exit", "clear", "help", "history", "cc", "bios", "make"};

/* Future per-command fields (cmd_journal.h). Empty: no command records more than its category. */
static const fl_cmdlog_field_allow_t k_field_allow[1] = {{NULL, NULL}};

size_t fl_cmdlog_allowed_field_count(void) {
    return k_field_allow[0].command ? sizeof k_field_allow / sizeof k_field_allow[0] : 0u;
}

static int known_command(const char *word) {
    if (fl_shell_cmd_lookup(word) != FL_SCMD_UNKNOWN)
        return 1;
    for (size_t i = 0; i < sizeof k_line_builtins / sizeof k_line_builtins[0]; i++)
        if (strcmp(word, k_line_builtins[i]) == 0)
            return 1;
    return 0;
}

const char *fl_cmdlog_category(const char *line, char *out, size_t cap) {
    char buf[CMDLOG_FIRST_WORD_SCAN];
    char *argv[2] = {NULL, NULL};
    size_t n;

    if (!out || cap == 0)
        return out;
    snprintf(out, cap, "other");
    if (!line)
        return out;
    /* Only the start of the line is copied (a known command name is short); the copy is wiped.
     * A first word cut by the scan limit could at worst match a shorter command name: a wrong
     * category, never a leak. */
    n = strnlen(line, sizeof buf - 1u);
    mem_domain_copy(buf, line, n);
    buf[n] = '\0';
    if (fl_shell_tokenize_line(buf, argv, 2) >= 1 && argv[0] && strlen(argv[0]) < cap &&
        strlen(argv[0]) < FL_CMDLOG_CATEGORY_MAX && known_command(argv[0]))
        snprintf(out, cap, "%s", argv[0]);
    mem_domain_secure_zero(buf, sizeof buf);
    return out;
}

/* ---------------------------------------------------------------------------------------------
 * Append
 * -------------------------------------------------------------------------------------------*/

/* Caller holds j->mu. Adds rec as the next record; 0, or -1 with a reason. */
static int append_locked(fl_cmdlog_t *j, const uint8_t *rec, size_t len, char *reason, size_t cap) {
    unsigned idx;
    size_t base;
    char path[CMDLOG_PATH_MAX];

    if (len > j->lim.record_max) {
        set_reason(reason, cap, "command log record is over its size bound");
        return -1;
    }
    if (j->total + len > j->lim.total_max) {
        set_reason(reason, cap, "command log is full (total size bound)");
        return -1;
    }
    if (j->segments == 0 || j->seg_len + len > j->lim.segment_max) {
        if (j->segments >= j->lim.segments_max) {
            set_reason(reason, cap, "command log is full (segment retention bound)");
            return -1;
        }
        if (j->tail_pending && j->segments > 0) {
            /* The active segment may end in a partial record: rewrite it without it before moving
             * on, or the partial record would sit mid-journal and fail verification. */
            if (seg_path(j->dir, j->segments - 1u, path, sizeof path) != 0 ||
                j->st->write_atomic(j->st->ctx, path, j->seg, j->seg_len) != 0) {
                set_reason(reason, cap, "command log storage write failed (dropping a torn tail)");
                return -1;
            }
            j->tail_pending = 0;
        }
        idx = j->segments; /* a new segment */
        base = 0;
        /* Never write over a segment this handle did not make: open proved none exists past the
         * last, and only our own failed write (next_dirty) may have left one behind. */
        if (!j->next_dirty && seg_exists(j->st, j->dir, idx) != 0) {
            set_reason(reason, cap, "command log found an unexpected later segment");
            return -1;
        }
    } else {
        idx = j->segments - 1u;
        base = j->seg_len;
    }
    if (seg_path(j->dir, idx, path, sizeof path) != 0) {
        set_reason(reason, cap, "command log path is too long");
        return -1;
    }
    if (base)
        mem_domain_copy(j->scratch, j->seg, base);
    mem_domain_copy(j->scratch + base, rec, len);
    if (j->st->write_atomic(j->st->ctx, path, j->scratch, base + len) != 0) {
        /* On a backend that is not atomic the file may now end in part of this record. j->seg
         * still holds the good bytes, so the next write (or roll) rewrites it first. */
        if (idx == j->segments)
            j->next_dirty = 1;
        else
            j->tail_pending = 1;
        set_reason(reason, cap, "command log storage write failed");
        return -1;
    }
    if (idx == j->segments) { /* moved to a new segment */
        j->segments++;
        j->seg_len = 0;
        j->next_dirty = 0;
    }
    {
        uint8_t *t = j->seg;
        j->seg = j->scratch;
        j->scratch = t;
    }
    j->total += len; /* j->total already leaves out a torn tail, which this rewrite dropped */
    j->seg_len = base + len;
    j->tail_pending = 0;
    return 0;
}

/* Builds, signs and appends one record. corr: the attempt's id for a close, or NULL for an ATTEMPT
 * (its new event id is written to corr_out). */
static int emit(fl_cmdlog_t *j, uint8_t type, const char *principal, const char *category, int32_t rc,
                const fl_cmdlog_corr_t *corr, fl_cmdlog_corr_t *corr_out, char *reason, size_t cap) {
    uint8_t rec[FL_CMDLOG_RECORD_MAX];
    uint8_t event_id[FL_CMDLOG_ID_LEN];
    uint8_t sig[FL_CMDLOG_SIG_MAX];
    uint8_t ver[2], hash[FL_CMDLOG_HASH_LEN];
    uint8_t u8;
    size_t sig_len = 0;
    int64_t now = 0;
    fl_bytes_sticky_writer_t w;
    int ret = -1;

    if (fl_random_bytes(event_id, sizeof event_id) != 0) {
        set_reason(reason, cap, "command log could not draw an event id");
        return -1;
    }
    if (fl_time_wall_sec(&now) != FL_RESULT_OK)
        now = 0;
    fl_net_put_u16_be(ver, FL_CMDLOG_FORMAT_VERSION);
    pthread_mutex_lock(&j->mu);
    fl_bytes_sticky_init(&w, rec, (uint16_t)j->lim.record_max);
    (void)fl_bytes_sticky_put_u32(&w, 0); /* record_len, filled below */
    (void)fl_bytes_sticky_put_bytes(&w, CMDLOG_MAGIC, 4);
    (void)fl_bytes_sticky_put_bytes(&w, ver, sizeof ver);
    (void)fl_bytes_sticky_put_str8(&w, FL_CMDLOG_DOMAIN);
    (void)fl_bytes_sticky_put_bytes(&w, j->journal_id, FL_CMDLOG_ID_LEN);
    (void)fl_bytes_sticky_put_bytes(&w, j->device_id, FL_CMDLOG_ID_LEN);
    (void)fl_bytes_sticky_put_u64(&w, j->next_seq);
    (void)fl_bytes_sticky_put_bytes(&w, j->last_hash, FL_CMDLOG_HASH_LEN);
    (void)fl_bytes_sticky_put_bytes(&w, event_id, FL_CMDLOG_ID_LEN);
    (void)fl_bytes_sticky_put_str8(&w, j->signer->key_id);
    if (principal && strlen(principal) >= FL_CMDLOG_LABEL_MAX)
        w.bad = 1; /* the reader's bound: label fields hold fewer than FL_CMDLOG_LABEL_MAX bytes */
    (void)fl_bytes_sticky_put_str8(&w, principal ? principal : "");
    u8 = type;
    (void)fl_bytes_sticky_put_bytes(&w, &u8, 1);
    (void)fl_bytes_sticky_put_str8(&w, category ? category : "");
    (void)fl_bytes_sticky_put_u32(&w, (uint32_t)rc);
    (void)fl_bytes_sticky_put_u64(&w, (uint64_t)now);
    u8 = FL_CMDLOG_TIME_LOCAL;
    (void)fl_bytes_sticky_put_bytes(&w, &u8, 1);
    (void)fl_bytes_sticky_put_bytes(&w, corr ? corr->id : event_id, FL_CMDLOG_ID_LEN);
    u8 = 0; /* no co-signature yet (sudo co-signing: kq_sudo_sign, later) */
    (void)fl_bytes_sticky_put_bytes(&w, &u8, 1);
    if (w.bad) {
        set_reason(reason, cap, "command log record does not fit (principal too long?)");
        goto out;
    }
    if (j->signer->sign(j->signer->ctx, FL_CMDLOG_DOMAIN, rec + 4, (size_t)w.w.len - 4u, sig, &sig_len) != 0 ||
        sig_len == 0 || sig_len > FL_CMDLOG_SIG_MAX) {
        set_reason(reason, cap, "command log signer refused to sign");
        goto out;
    }
    u8 = (uint8_t)sig_len;
    (void)fl_bytes_sticky_put_bytes(&w, &u8, 1);
    (void)fl_bytes_sticky_put_bytes(&w, sig, sig_len);
    if (w.bad) {
        set_reason(reason, cap, "command log record is over its size bound");
        goto out;
    }
    fl_net_put_u32_be(rec, (uint32_t)w.w.len);
    if (sha256(rec, w.w.len, hash) != 0) {
        set_reason(reason, cap, "command log could not hash the record");
        goto out;
    }
    if (append_locked(j, rec, w.w.len, reason, cap) != 0)
        goto out;
    mem_domain_copy(j->last_hash, hash, sizeof hash);
    j->next_seq++;
    if (corr_out)
        mem_domain_copy(corr_out->id, event_id, FL_CMDLOG_ID_LEN);
    ret = 0;
out:
    pthread_mutex_unlock(&j->mu);
    mem_domain_secure_zero(rec, sizeof rec);
    return ret;
}

/* ---------------------------------------------------------------------------------------------
 * Caller API
 * -------------------------------------------------------------------------------------------*/

fl_cmdlog_decision_t fl_cmdlog_attempt(fl_cmdlog_t *j, const char *principal, const char *line,
                                       fl_cmdlog_corr_t *corr, char *reason, size_t reason_cap) {
    char category[FL_CMDLOG_CATEGORY_MAX];

    set_reason(reason, reason_cap, "");
    if (corr)
        mem_domain_zero(corr, sizeof *corr);
    if (!j || !corr) {
        set_reason(reason, reason_cap, "the command log is not open");
        return FL_CMDLOG_REFUSE;
    }
    fl_cmdlog_category(line, category, sizeof category);
    /* Owner decision (Nov 16): refuse with a red error giving the reason. */
    if (emit(j, FL_CMDLOG_EV_ATTEMPT, principal, category, 0, NULL, corr, reason, reason_cap) != 0)
        return FL_CMDLOG_REFUSE;
    return FL_CMDLOG_ALLOW;
}

static int close_attempt(fl_cmdlog_t *j, uint8_t type, const fl_cmdlog_corr_t *corr, int code, char *reason,
                         size_t cap) {
    set_reason(reason, cap, "");
    if (!j || !corr) {
        set_reason(reason, cap, "the command log is not open");
        return -1;
    }
    /* No principal or category: the attempt it closes carries them. */
    return emit(j, type, "", "", (int32_t)code, corr, NULL, reason, cap);
}

int fl_cmdlog_complete(fl_cmdlog_t *j, const fl_cmdlog_corr_t *corr, int rc, char *reason, size_t reason_cap) {
    return close_attempt(j, FL_CMDLOG_EV_COMPLETION, corr, rc, reason, reason_cap);
}

int fl_cmdlog_refused(fl_cmdlog_t *j, const fl_cmdlog_corr_t *corr, int code, char *reason, size_t reason_cap) {
    return close_attempt(j, FL_CMDLOG_EV_REFUSAL, corr, code, reason, reason_cap);
}

void fl_cmdlog_print_refusal(const char *reason) {
    fl_color_error("command refused: the signed command log could not be written (%s)",
                   reason && reason[0] ? reason : "unknown reason");
}

void fl_cmdlog_print_completion_gap(const char *reason) {
    fl_color_warn_local("the command ran, but its completion was not written to the signed command log (%s); "
                        "the log will show its outcome as unknown",
                        reason && reason[0] ? reason : "unknown reason");
}
