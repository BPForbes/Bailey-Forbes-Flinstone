/*
 * Signed command log (FL-04): see cmd_journal.h for the record format, the storage and recovery
 * semantics, the bounds and the failure policy. Hosted only.
 *
 * Reuse: storage is the KeyQuorum checkpoints' table (fl_kq_storage_t); the hash is OpenSSL SHA-256
 * as elsewhere in the tree; little-endian fields use the net_endian.h helpers; the category
 * allowlist is the shell's own command registry (fl_shell_cmd_lookup) and the first word is found
 * with fl_shell_tokenize_line; random ids come from fl_random_bytes; the wall clock from
 * fl_time_wall_sec; messages through fl_color_error / fl_color_vfprintf_tagged; every copy, zeroing and equality
 * goes through the ASM-backed mem_domain_* wrappers.
 */
#include "cmd_journal.h"

#include "cmd_decl.h"
#include "fl/random.h"
#include "fl_colors.h"
#include "mem_domain.h"
#include "net_endian.h"
#include "shell_tokenize.h"
#include "timekeeping.h"

#include <openssl/sha.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CMDLOG_MAGIC "FLCL"
#define CMDLOG_PATH_MAX 256u
#define CMDLOG_FIRST_WORD_SCAN 256u /* only this much of a line is ever copied, then wiped */

struct fl_cmdlog {
    pthread_mutex_t mu;
    const fl_kq_storage_t *st;
    char dir[CMDLOG_PATH_MAX];
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
    int tail_pending;  /* the active segment on storage still ends in a torn record */
};

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

static void set_reason(char *reason, size_t cap, const char *text) {
    if (reason && cap)
        snprintf(reason, cap, "%s", text);
}

/* ---------------------------------------------------------------------------------------------
 * Encoding
 * -------------------------------------------------------------------------------------------*/

typedef struct {
    uint8_t *b;
    size_t cap, len;
    int err;
} wr_t;

static void w_bytes(wr_t *w, const void *p, size_t n) {
    if (w->err || n > w->cap - w->len) {
        w->err = 1;
        return;
    }
    if (n)
        mem_domain_copy(w->b + w->len, p, n);
    w->len += n;
}
static void w_u8(wr_t *w, uint8_t v) { w_bytes(w, &v, 1); }
static void w_u16(wr_t *w, uint16_t v) { uint8_t t[2]; fl_net_put_u16_le(t, v); w_bytes(w, t, 2); }
static void w_u32(wr_t *w, uint32_t v) { uint8_t t[4]; fl_net_put_u32_le(t, v); w_bytes(w, t, 4); }
static void w_u64(wr_t *w, uint64_t v) { uint8_t t[8]; fl_net_put_u64_le(t, v); w_bytes(w, t, 8); }
static void w_str(wr_t *w, const char *s, size_t max) {
    size_t n = s ? strlen(s) : 0;

    if (n >= max) {
        w->err = 1;
        return;
    }
    w_u8(w, (uint8_t)n);
    w_bytes(w, s, n);
}

/* ---------------------------------------------------------------------------------------------
 * Parsing
 * -------------------------------------------------------------------------------------------*/

typedef struct {
    size_t len;        /* whole record */
    size_t signed_off; /* signed bytes: [signed_off, signed_off + signed_len) */
    size_t signed_len;
    const uint8_t *domain;
    uint8_t domain_len;
    uint16_t version;
    const uint8_t *journal_id, *device_id, *prev_hash, *event_id, *corr;
    uint64_t seq;
    char key_id[FL_CMDLOG_LABEL_MAX];
    uint8_t type;
    int32_t rc;
    int cosig;
    const uint8_t *sig;
    uint8_t sig_len;
} rec_t;

typedef struct {
    const uint8_t *b;
    size_t len, pos;
    int err;
} rd_t;

static const uint8_t *r_bytes(rd_t *r, size_t n) {
    const uint8_t *p;

    if (r->err || n > r->len - r->pos) {
        r->err = 1;
        return NULL;
    }
    p = r->b + r->pos;
    r->pos += n;
    return p;
}
static uint8_t r_u8(rd_t *r) { const uint8_t *p = r_bytes(r, 1); return p ? p[0] : 0; }
static uint16_t r_u16(rd_t *r) { const uint8_t *p = r_bytes(r, 2); return p ? fl_net_get_u16_le(p) : 0; }
static uint32_t r_u32(rd_t *r) { const uint8_t *p = r_bytes(r, 4); return p ? fl_net_get_u32_le(p) : 0; }
static uint64_t r_u64(rd_t *r) { const uint8_t *p = r_bytes(r, 8); return p ? fl_net_get_u64_le(p) : 0; }
/* A length-prefixed string, copied NUL-terminated into out (cap bytes) when out is given. */
static void r_str(rd_t *r, char *out, size_t cap) {
    uint8_t n = r_u8(r);
    const uint8_t *p = r_bytes(r, n);

    if (!p || n >= cap) {
        r->err = 1;
        return;
    }
    if (out) {
        mem_domain_copy(out, p, n);
        out[n] = '\0';
    }
}

/* 0 parsed; -1 malformed; -2 incomplete (the bytes end inside a record). */
static int parse_record(const uint8_t *p, size_t avail, size_t record_max, rec_t *out) {
    rd_t r;
    uint32_t len;
    const uint8_t *magic;

    mem_domain_zero(out, sizeof *out);
    if (avail < 4)
        return -2;
    len = fl_net_get_u32_le(p);
    if (len < 8 || len > record_max)
        return -1;
    if (len > avail)
        return -2;
    r.b = p;
    r.len = len;
    r.pos = 4;
    r.err = 0;
    out->len = len;
    out->signed_off = 4;
    magic = r_bytes(&r, 4);
    if (!magic || !mem_domain_equal(magic, CMDLOG_MAGIC, 4))
        return -1;
    out->version = r_u16(&r);
    out->domain_len = r_u8(&r);
    out->domain = r_bytes(&r, out->domain_len);
    out->journal_id = r_bytes(&r, FL_CMDLOG_ID_LEN);
    out->device_id = r_bytes(&r, FL_CMDLOG_ID_LEN);
    out->seq = r_u64(&r);
    out->prev_hash = r_bytes(&r, FL_CMDLOG_HASH_LEN);
    out->event_id = r_bytes(&r, FL_CMDLOG_ID_LEN);
    r_str(&r, out->key_id, sizeof out->key_id);
    r_str(&r, NULL, FL_CMDLOG_LABEL_MAX);    /* principal */
    out->type = r_u8(&r);
    r_str(&r, NULL, FL_CMDLOG_CATEGORY_MAX); /* category */
    out->rc = (int32_t)r_u32(&r);
    (void)r_u64(&r);                          /* timestamp */
    (void)r_u8(&r);                           /* time trust */
    out->corr = r_bytes(&r, FL_CMDLOG_ID_LEN);
    out->cosig = r_u8(&r);
    if (out->cosig == 1) {
        (void)r_bytes(&r, FL_CMDLOG_ID_LEN); /* nonce */
        r_str(&r, NULL, FL_CMDLOG_LABEL_MAX);
        r_str(&r, NULL, FL_CMDLOG_SIG_MAX + 1u);
    } else if (out->cosig != 0) {
        return -1;
    }
    out->signed_len = r.pos - out->signed_off;
    out->sig_len = r_u8(&r);
    out->sig = r_bytes(&r, out->sig_len);
    if (r.err || out->sig_len == 0 || out->sig_len > FL_CMDLOG_SIG_MAX || r.pos != len)
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

/*
 * Walks the journal. When keep_last is given, *keep_last receives the last segment's verified
 * bytes (MEM_DOMAIN_FS memory, free_fs) and *keep_len their length.
 */
static int walk(const fl_kq_storage_t *st, const char *dir, const fl_cmdlog_signer_t *signer,
                const fl_cmdlog_limits_t *lim, fl_cmdlog_report_t *rep, uint8_t **keep_last,
                size_t *keep_len) {
    static const uint8_t zero_hash[FL_CMDLOG_HASH_LEN] = {0};
    open_set_t open = {0};
    const size_t domain_len = strlen(FL_CMDLOG_DOMAIN);
    int have_first = 0;

    mem_domain_zero(rep, sizeof *rep);
    if (keep_last) {
        *keep_last = NULL;
        *keep_len = 0;
    }
    for (unsigned idx = 0; idx < lim->segments_max; idx++) {
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
        while (pos < len && rep->status == FL_CMDLOG_V_OK) {
            rec_t rec;
            const uint8_t *p = data + pos;
            int pr = parse_record(p, len - pos, lim->record_max, &rec);

            if (pr == -2) {
                char next[CMDLOG_PATH_MAX];

                /* A partial record is a torn tail only in the last segment. */
                if (seg_path(dir, idx + 1u, next, sizeof next) == 0 && st->exists(st->ctx, next) == 0) {
                    rep->tail_incomplete = 1;
                    rep->tail_bytes = len - pos;
                } else {
                    rep->status = FL_CMDLOG_V_MALFORMED;
                }
                break;
            }
            if (pr != 0 || rec.version != FL_CMDLOG_FORMAT_VERSION || rec.domain_len != domain_len ||
                !mem_domain_equal(rec.domain, FL_CMDLOG_DOMAIN, domain_len) ||
                (rec.type != FL_CMDLOG_EV_ATTEMPT && rec.type != FL_CMDLOG_EV_COMPLETION &&
                 rec.type != FL_CMDLOG_EV_REFUSAL)) {
                rep->status = FL_CMDLOG_V_MALFORMED;
                break;
            }
            if (signer->verify(signer->ctx, rec.key_id, FL_CMDLOG_DOMAIN, p + rec.signed_off,
                               rec.signed_len, rec.sig, rec.sig_len) != 1) {
                rep->status = FL_CMDLOG_V_BAD_SIGNATURE;
                break;
            }
            if (!have_first) {
                mem_domain_copy(rep->journal_id, rec.journal_id, FL_CMDLOG_ID_LEN);
                have_first = 1;
            } else if (!mem_domain_equal(rep->journal_id, rec.journal_id, FL_CMDLOG_ID_LEN)) {
                rep->status = FL_CMDLOG_V_FOREIGN;
                break;
            }
            if (rec.seq != rep->next_seq) {
                rep->status = FL_CMDLOG_V_BAD_SEQUENCE;
                break;
            }
            if (!mem_domain_equal(rec.prev_hash, rep->next_seq == 0 ? zero_hash : rep->last_hash,
                                  FL_CMDLOG_HASH_LEN)) {
                rep->status = FL_CMDLOG_V_BROKEN_CHAIN;
                break;
            }
            if (rec.type == FL_CMDLOG_EV_ATTEMPT) {
                if (!mem_domain_equal(rec.corr, rec.event_id, FL_CMDLOG_ID_LEN)) {
                    rep->status = FL_CMDLOG_V_MALFORMED;
                    break;
                }
                if (open_add(&open, rec.corr) != 0) {
                    rep->status = FL_CMDLOG_V_IO;
                    break;
                }
            } else if (!open_close(&open, rec.corr)) {
                rep->orphan_closes++;
            }
            if (rec.cosig)
                rep->cosigs_unverified++;
            SHA256(p, rec.len, rep->last_hash);
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
        if (rep->tail_incomplete)
            break;
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
                     const fl_cmdlog_limits_t *limits, fl_cmdlog_report_t *report) {
    fl_cmdlog_limits_t lim = limits_or_default(limits);
    if (!report || !storage || !dir || !signer || !signer->verify)
        return -1;
    return walk(storage, dir, signer, &lim, report, NULL, NULL);
}

/* ---------------------------------------------------------------------------------------------
 * Open / close
 * -------------------------------------------------------------------------------------------*/

int fl_cmdlog_open(const fl_cmdlog_config_t *cfg, fl_cmdlog_t **out, fl_cmdlog_report_t *report) {
    fl_cmdlog_report_t local;
    fl_cmdlog_report_t *rep = report ? report : &local;
    fl_cmdlog_t *j;
    uint8_t *last = NULL;
    size_t last_len = 0;

    if (out)
        *out = NULL;
    if (!cfg || !out || !cfg->storage || !cfg->dir || !cfg->signer || !cfg->signer->sign || !cfg->signer->verify ||
        !cfg->signer->key_id || strlen(cfg->signer->key_id) >= FL_CMDLOG_LABEL_MAX ||
        strlen(cfg->dir) >= CMDLOG_PATH_MAX)
        return -1;
    j = (fl_cmdlog_t *)mem_domain_calloc(MEM_DOMAIN_FS, 1, sizeof *j);
    if (!j)
        return -1;
    j->st = cfg->storage;
    j->lim = limits_or_default(&cfg->limits);
    j->signer = cfg->signer;
    mem_domain_copy(j->dir, cfg->dir, strlen(cfg->dir) + 1u);
    mem_domain_copy(j->device_id, cfg->device_id, FL_CMDLOG_ID_LEN);
    j->seg = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, j->lim.segment_max);
    j->scratch = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, j->lim.segment_max);
    if (!j->seg || !j->scratch || pthread_mutex_init(&j->mu, NULL) != 0) {
        free_fs(j->seg);
        free_fs(j->scratch);
        mem_domain_free(MEM_DOMAIN_FS, j);
        return -1;
    }
    if (walk(j->st, j->dir, j->signer, &j->lim, rep, &last, &last_len) != 0) {
        fl_cmdlog_close(j); /* evidence: never append over a journal that does not verify */
        return -1;
    }
    if (rep->records == 0 && fl_random_bytes(j->journal_id, FL_CMDLOG_ID_LEN) != 0) {
        free_fs(last);
        fl_cmdlog_close(j);
        return -1;
    }
    if (rep->records)
        mem_domain_copy(j->journal_id, rep->journal_id, FL_CMDLOG_ID_LEN);
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
    return 0;
}

void fl_cmdlog_close(fl_cmdlog_t *j) {
    if (!j)
        return;
    pthread_mutex_destroy(&j->mu);
    mem_domain_secure_zero(j->seg, j->lim.segment_max);
    mem_domain_secure_zero(j->scratch, j->lim.segment_max);
    free_fs(j->seg);
    free_fs(j->scratch);
    mem_domain_secure_zero(j, sizeof *j);
    mem_domain_free(MEM_DOMAIN_FS, j);
}

/* ---------------------------------------------------------------------------------------------
 * Redaction
 * -------------------------------------------------------------------------------------------*/

/* Line-mode builtins the interpreter parses before argv (cmd_decl.h *_maybe); every other known
 * name comes from the command registry. */
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
    /* Only the start of the line is copied (a known command name is short); the copy is wiped. */
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
            /* The active segment ends in a torn record: rewrite it without it before moving on, or
             * the partial record would sit mid-journal and fail verification. */
            if (seg_path(j->dir, j->segments - 1u, path, sizeof path) != 0 ||
                j->st->write_atomic(j->st->ctx, path, j->seg, j->seg_len) != 0) {
                set_reason(reason, cap, "command log storage write failed (dropping a torn tail)");
                return -1;
            }
            j->tail_pending = 0;
        }
        idx = j->segments; /* a new segment */
        base = 0;
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
        set_reason(reason, cap, "command log storage write failed");
        return -1;
    }
    if (idx == j->segments) { /* moved to a new segment */
        j->segments++;
        j->seg_len = 0;
    }
    {
        uint8_t *t = j->seg;
        j->seg = j->scratch;
        j->scratch = t;
    }
    j->total += len; /* j->total already leaves out a torn tail, which this rewrite dropped */
    j->seg_len = base + len;
    j->tail_pending = 0; /* rewritten without it, or rewritten before the move to a new segment */
    return 0;
}

/* Builds, signs and appends one record. corr: the attempt's id for a close, or NULL for an ATTEMPT
 * (its new event id is written to corr_out). */
static int emit(fl_cmdlog_t *j, uint8_t type, const char *principal, const char *category, int32_t rc,
                const fl_cmdlog_corr_t *corr, fl_cmdlog_corr_t *corr_out, char *reason, size_t cap) {
    uint8_t rec[FL_CMDLOG_RECORD_MAX];
    uint8_t event_id[FL_CMDLOG_ID_LEN];
    uint8_t sig[FL_CMDLOG_SIG_MAX];
    size_t sig_len = 0;
    int64_t now = 0;
    wr_t w = {rec, sizeof rec, 0, 0};
    int ret = -1;

    if (fl_random_bytes(event_id, sizeof event_id) != 0) {
        set_reason(reason, cap, "command log could not draw an event id");
        return -1;
    }
    if (fl_time_wall_sec(&now) != FL_RESULT_OK)
        now = 0;
    pthread_mutex_lock(&j->mu);
    w_u32(&w, 0); /* record_len, filled below */
    w_bytes(&w, CMDLOG_MAGIC, 4);
    w_u16(&w, FL_CMDLOG_FORMAT_VERSION);
    w_str(&w, FL_CMDLOG_DOMAIN, 256u);
    w_bytes(&w, j->journal_id, FL_CMDLOG_ID_LEN);
    w_bytes(&w, j->device_id, FL_CMDLOG_ID_LEN);
    w_u64(&w, j->next_seq);
    w_bytes(&w, j->last_hash, FL_CMDLOG_HASH_LEN);
    w_bytes(&w, event_id, FL_CMDLOG_ID_LEN);
    w_str(&w, j->signer->key_id, FL_CMDLOG_LABEL_MAX);
    w_str(&w, principal ? principal : "", FL_CMDLOG_LABEL_MAX);
    w_u8(&w, type);
    w_str(&w, category ? category : "", FL_CMDLOG_CATEGORY_MAX);
    w_u32(&w, (uint32_t)rc);
    w_u64(&w, (uint64_t)now);
    w_u8(&w, FL_CMDLOG_TIME_LOCAL);
    w_bytes(&w, corr ? corr->id : event_id, FL_CMDLOG_ID_LEN);
    w_u8(&w, 0); /* no co-signature yet (sudo co-signing: kq_sudo_sign, later) */
    if (w.err) {
        set_reason(reason, cap, "command log record does not fit (principal too long?)");
        goto out;
    }
    if (j->signer->sign(j->signer->ctx, FL_CMDLOG_DOMAIN, rec + 4, w.len - 4u, sig, &sig_len) != 0 ||
        sig_len == 0 || sig_len > FL_CMDLOG_SIG_MAX) {
        set_reason(reason, cap, "command log signer refused to sign");
        goto out;
    }
    w_u8(&w, (uint8_t)sig_len);
    w_bytes(&w, sig, sig_len);
    if (w.err) {
        set_reason(reason, cap, "command log record is over its size bound");
        goto out;
    }
    fl_net_put_u32_le(rec, (uint32_t)w.len);
    if (append_locked(j, rec, w.len, reason, cap) != 0)
        goto out;
    SHA256(rec, w.len, j->last_hash);
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

/* Yellow, like fl_color_warn, but tagged as a local warning rather than "[Server] ". */
static void warn_local(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    fl_color_vfprintf_tagged(stderr, KYEL, "[WARNING] ", fmt, ap);
    va_end(ap);
}

void fl_cmdlog_print_completion_gap(const char *reason) {
    warn_local("the command ran, but its completion was not written to the signed command log (%s); "
               "the log will show its outcome as unknown",
               reason && reason[0] ? reason : "unknown reason");
}
