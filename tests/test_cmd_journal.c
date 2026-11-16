/**
 * test_cmd_journal.c - the signed command log (userland/shell/cmd_journal.c), over the in-memory
 * fl_kq_storage_t.
 *
 * No literal secrets: every secret-like value is drawn at run time (fl_random_bytes) and hex-encoded.
 */
#include "cmd_journal.h"
#include "fl/random.h"
#include "fl_colors.h"
#include "kq_mem_storage.h"
#include "mem_domain.h"
#include "tests/kq_test_util.h"

#include <openssl/evp.h>
#include <openssl/params.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

#define DIR_ "/cmdlog"
#define WHO "Root.Flinstone.Mya"

/* ------------------------------------------------------------------------------------------------
 * TEST-ONLY signer: HMAC-SHA256 over (domain || 0x00 || message) with a key drawn at start. It stands
 * in for KeyQuorum's domain-separated signature, which has no C API yet (see cmd_journal.h). Never
 * use it outside tests: a MAC key that verifies can also forge.
 * ----------------------------------------------------------------------------------------------*/
typedef struct {
    uint8_t key[32];
    const char *id;
} test_hmac_t;

/* Two accounts' keys. The verifier knows both, as a KeyQuorum registry would: a journal must still
 * accept records only from its own signer. */
static test_hmac_t s_keys[2] = {{{0}, "test-hmac-signer"}, {{0}, "test-hmac-signer-b"}};

static int hmac_domain(const uint8_t key[32], const char *domain, const uint8_t *msg, size_t len,
                       uint8_t out[32]) {
    int ok;
    EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    EVP_MAC_CTX *c = mac ? EVP_MAC_CTX_new(mac) : NULL;
    OSSL_PARAM params[2] = {OSSL_PARAM_construct_utf8_string("digest", (char *)"SHA256", 0), OSSL_PARAM_construct_end()};
    size_t outl = 0;
    const uint8_t sep = 0;

    ok = c && EVP_MAC_init(c, key, 32, params) == 1 &&
         EVP_MAC_update(c, (const uint8_t *)domain, strlen(domain)) == 1 && EVP_MAC_update(c, &sep, 1) == 1 &&
         EVP_MAC_update(c, msg, len) == 1 && EVP_MAC_final(c, out, &outl, 32) == 1 && outl == 32;
    EVP_MAC_CTX_free(c);
    EVP_MAC_free(mac);
    return ok ? 0 : -1;
}

static int t_sign(void *ctx, const char *domain, const uint8_t *msg, size_t len, uint8_t *sig, size_t *sig_len) {
    if (hmac_domain(((const test_hmac_t *)ctx)->key, domain, msg, len, sig) != 0)
        return -1;
    *sig_len = 32;
    return 0;
}

/* Verifies under whichever known key key_id names (ctx unused). */
static int t_verify(void *ctx, const char *key_id, const char *domain, const uint8_t *msg, size_t len,
                    const uint8_t *sig, size_t sig_len) {
    uint8_t want[32];

    (void)ctx;
    for (size_t i = 0; i < 2; i++) {
        if (strcmp(key_id, s_keys[i].id) != 0)
            continue;
        if (sig_len != 32 || hmac_domain(s_keys[i].key, domain, msg, len, want) != 0)
            return 0;
        return mem_domain_equal(want, sig, 32);
    }
    return 0;
}

static const fl_cmdlog_signer_t s_signer = {&s_keys[0], "test-hmac-signer", t_sign, t_verify};
static const fl_cmdlog_signer_t s_signer_b = {&s_keys[1], "test-hmac-signer-b", t_sign, t_verify};

/* ------------------------------------------------------------------------------------------------
 * A storage over the memory table that can fail every write, write half then fail (a backend that is
 * not atomic), or hide one segment (a missing middle segment; the memory table has no remove).
 * ----------------------------------------------------------------------------------------------*/
static int s_fail_writes, s_partial_writes;
static const char *s_hidden;
static int hidden(const char *p) { return s_hidden && strcmp(p, s_hidden) == 0; }
static int f_read(void *c, const char *p, size_t m, uint8_t **d, size_t *l) {
    (void)c;
    return hidden(p) ? -3 : fl_kq_storage_memory()->read_file(NULL, p, m, d, l);
}
static int f_write(void *c, const char *p, const uint8_t *d, size_t l) {
    (void)c;
    if (s_partial_writes) {
        (void)fl_kq_storage_memory()->write_atomic(NULL, p, d, l / 2u);
        return -1;
    }
    return s_fail_writes ? -1 : fl_kq_storage_memory()->write_atomic(NULL, p, d, l);
}
static int f_exists(void *c, const char *p) {
    (void)c;
    return hidden(p) ? 0 : fl_kq_storage_memory()->exists(NULL, p);
}
static const fl_kq_storage_t s_failing = {NULL, f_read, f_write, f_exists, NULL, NULL, NULL, NULL, NULL, NULL};

static const fl_kq_storage_t *mem(void) { return fl_kq_storage_memory(); }

static void fresh(void) {
    fl_kq_storage_memory_reset();
    s_fail_writes = 0;
    s_partial_writes = 0;
    s_hidden = NULL;
}

static fl_cmdlog_config_t config(const fl_kq_storage_t *st, const fl_cmdlog_limits_t *lim) {
    fl_cmdlog_config_t c;

    memset(&c, 0, sizeof c);
    c.storage = st;
    c.dir = DIR_;
    c.signer = &s_signer;
    memset(c.device_id, 0xd1, sizeof c.device_id);
    if (lim)
        c.limits = *lim;
    return c;
}

static int run_one(fl_cmdlog_t *j, const char *line, int rc) {
    fl_cmdlog_corr_t corr;
    char why[FL_CMDLOG_REASON_MAX];

    if (fl_cmdlog_attempt(j, WHO, line, &corr, why, sizeof why) != FL_CMDLOG_ALLOW)
        return -1;
    return fl_cmdlog_complete(j, &corr, rc, why, sizeof why);
}

static int seg_read(unsigned idx, uint8_t **d, size_t *n) {
    char p[64];

    snprintf(p, sizeof p, DIR_ "/cmdlog-%06u.seg", idx);
    return mem()->read_file(NULL, p, 1u << 20, d, n);
}

static int seg_write(unsigned idx, const uint8_t *d, size_t n) {
    char p[64];

    snprintf(p, sizeof p, DIR_ "/cmdlog-%06u.seg", idx);
    return mem()->write_atomic(NULL, p, d, n);
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3]; }

static int contains(const uint8_t *hay, size_t n, const char *needle, size_t m) {
    for (size_t i = 0; m && i + m <= n; i++)
        if (memcmp(hay + i, needle, m) == 0)
            return 1;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ */

static int test_a_good_chain_verifies_and_reopens(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, &r) == 0 && r.records == 0);
    for (int i = 0; i < 5; i++)
        ASSERT(run_one(j, "dir /", i) == 0);
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == 0);
    ASSERT(r.status == FL_CMDLOG_V_OK && r.records == 10 && r.open_attempts == 0 && !r.tail_incomplete);
    ASSERT(r.next_seq == 10 && r.orphan_closes == 0);
    /* Reopen continues the same chain. */
    ASSERT(fl_cmdlog_open(&c, &j, &r) == 0 && r.records == 10);
    ASSERT(run_one(j, "whoami", 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == 0 && r.records == 12);
    return 0;
}

/* Record boundaries of segment 0. */
static size_t bounds(const uint8_t *d, size_t n, size_t *off, size_t max) {
    size_t k = 0, pos = 0;

    while (pos + 4 <= n && k < max) {
        off[k++] = pos;
        pos += be32(d + pos);
    }
    return k;
}

static int test_a_tampered_record_is_detected(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    uint8_t *d = NULL;
    size_t n = 0, off[16];

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    for (int i = 0; i < 3; i++)
        ASSERT(run_one(j, "cat a", 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(seg_read(0, &d, &n) == 0 && bounds(d, n, off, 16) == 6);
    d[off[2] + 105] ^= 0x01; /* a byte of record 2's event id: signed */
    ASSERT(seg_write(0, d, n) == 0);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == -1);
    ASSERT(r.status == FL_CMDLOG_V_BAD_SIGNATURE && r.records == 2 && r.bad_seq == 2);
    ASSERT(fl_cmdlog_open(&c, &j, &r) == -1 && j == NULL); /* evidence: not appended to */
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

static int test_a_reordered_record_is_detected(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    uint8_t *d = NULL, *swapped;
    size_t n = 0, off[16], a, b;

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    for (int i = 0; i < 3; i++)
        ASSERT(run_one(j, "mkdir x", 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(seg_read(0, &d, &n) == 0 && bounds(d, n, off, 16) == 6);
    a = off[3] - off[2];
    b = off[4] - off[3];
    swapped = malloc(n);
    ASSERT(swapped);
    memcpy(swapped, d, n);
    memcpy(swapped + off[2], d + off[3], b); /* records 2 and 3 change places */
    memcpy(swapped + off[2] + b, d + off[2], a);
    ASSERT(seg_write(0, swapped, n) == 0);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == -1);
    ASSERT(r.status == FL_CMDLOG_V_BAD_SEQUENCE && r.records == 2);
    free(swapped);
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

static int test_a_truncated_tail_is_reported_and_recovered(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    uint8_t *d = NULL;
    size_t n = 0;

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    for (int i = 0; i < 3; i++)
        ASSERT(run_one(j, "version", 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(seg_read(0, &d, &n) == 0);
    ASSERT(seg_write(0, d, n - 10) == 0); /* a torn last record */
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == 0);
    ASSERT(r.tail_incomplete == 1 && r.tail_bytes > 0 && r.records == 5);
    ASSERT(r.open_attempts == 1); /* the torn record was the last completion */
    /* Recovery: open reports it, the next append drops the torn bytes, the chain verifies clean. */
    ASSERT(fl_cmdlog_open(&c, &j, &r) == 0 && r.tail_incomplete == 1);
    ASSERT(run_one(j, "version", 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == 0);
    ASSERT(!r.tail_incomplete && r.records == 7 && r.open_attempts == 1);
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

static int test_an_attempt_without_completion_is_outcome_unknown(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    fl_cmdlog_corr_t a, b, lost;
    char why[FL_CMDLOG_REASON_MAX];

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    ASSERT(fl_cmdlog_attempt(j, WHO, "format", &a, why, sizeof why) == FL_CMDLOG_ALLOW);
    ASSERT(fl_cmdlog_attempt(j, WHO, "rmtree /x", &lost, why, sizeof why) == FL_CMDLOG_ALLOW);
    ASSERT(fl_cmdlog_attempt(j, WHO, "useradd z", &b, why, sizeof why) == FL_CMDLOG_ALLOW);
    ASSERT(fl_cmdlog_complete(j, &a, 0, why, sizeof why) == 0);
    ASSERT(fl_cmdlog_refused(j, &b, 13, why, sizeof why) == 0); /* REFUSAL closes an attempt too */
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == 0);
    ASSERT(r.records == 5 && r.open_attempts == 1 && memcmp(r.open[0].id, lost.id, sizeof lost.id) == 0);
    return 0;
}

static int test_secret_arguments_never_reach_storage(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL;
    char s1[65], s2[65], s3[65], s4[65], line[256], cat[FL_CMDLOG_CATEGORY_MAX];
    const char *secrets[4] = {s1, s2, s3, s4};
    uint8_t *d = NULL;
    size_t n = 0;

    fresh();
    ASSERT(kq_test_random_hex(s1, 24) == 0 && kq_test_random_hex(s2, 24) == 0 && kq_test_random_hex(s3, 24) == 0 && kq_test_random_hex(s4, 24) == 0);
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    snprintf(line, sizeof line, "passwd %s", s1);
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "passwd") == 0);
    ASSERT(run_one(j, line, 0) == 0);
    snprintf(line, sizeof line, "sudo -S \"%s\" passwd Root.Other", s2);
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "sudo") == 0);
    ASSERT(run_one(j, line, 1) == 0);
    snprintf(line, sizeof line, "%s --as-first-word", s3); /* a mistyped secret as the command */
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "other") == 0);
    ASSERT(run_one(j, line, 127) == 0);
    snprintf(line, sizeof line, "  login Root.Flinstone.Mya '%s'", s4);
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "login") == 0);
    ASSERT(run_one(j, line, 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(fl_kq_storage_memory_files() == 1 && seg_read(0, &d, &n) == 0 && n > 0);
    for (int i = 0; i < 4; i++) {
        ASSERT(!contains(d, n, secrets[i], strlen(secrets[i])));
        ASSERT(!contains(d, n, secrets[i], 8));                     /* nor any 8-character prefix */
        ASSERT(!contains(d, n, secrets[i] + strlen(secrets[i]) - 8, 8));
    }
    ASSERT(!contains(d, n, "Root.Other", 10) && !contains(d, n, "--as-first-word", 15));
    ASSERT(contains(d, n, "passwd", 6) && contains(d, n, "other", 5));
    mem_domain_secure_zero(d, n);
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

static int test_bounds_are_enforced(void) {
    fl_cmdlog_limits_t lim = {0, 1024, 0, 3};
    fl_cmdlog_config_t c = config(mem(), &lim);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    fl_cmdlog_corr_t corr;
    char why[FL_CMDLOG_REASON_MAX], big[200];
    int ok = 0;

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    /* Retention: three 1 KiB segments, then the journal is full and refuses. */
    while (ok < 100 && fl_cmdlog_attempt(j, WHO, "du", &corr, why, sizeof why) == FL_CMDLOG_ALLOW)
        ok++;
    ASSERT(ok > 3 && ok < 100 && strstr(why, "retention") != NULL);
    ASSERT(fl_kq_storage_memory_files() == 3);
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, &lim, &r) == 0); /* the chain spans the segments */
    ASSERT(r.segments == 3 && r.records == (uint64_t)ok && r.open_attempts == (unsigned)ok);

    /* Total bytes. */
    fresh();
    lim.segments_max = 0;
    lim.total_max = 900;
    c = config(mem(), &lim);
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    ok = 0;
    while (ok < 100 && fl_cmdlog_attempt(j, WHO, "du", &corr, why, sizeof why) == FL_CMDLOG_ALLOW)
        ok++;
    ASSERT(ok >= 1 && ok < 10 && strstr(why, "total") != NULL);
    /* One record: a principal over the label bound does not fit. */
    memset(big, 'p', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    fresh();
    c = config(mem(), NULL);
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    ASSERT(fl_cmdlog_attempt(j, big, "du", &corr, why, sizeof why) == FL_CMDLOG_REFUSE && why[0]);
    ASSERT(fl_kq_storage_memory_files() == 0);
    fl_cmdlog_close(j);
    return 0;
}

static int test_a_failed_attempt_write_refuses(void) {
    fl_cmdlog_config_t c = config(&s_failing, NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_corr_t corr;
    char why[FL_CMDLOG_REASON_MAX];

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    s_fail_writes = 1;
    ASSERT(fl_cmdlog_attempt(j, WHO, "rmtree /data", &corr, why, sizeof why) == FL_CMDLOG_REFUSE);
    ASSERT(why[0] != '\0' && strstr(why, "log") != NULL);
    fl_color_set_disabled(1);
    fl_cmdlog_print_refusal(why); /* "[ERROR] command refused: the signed command log could not ..." */
    ASSERT(fl_kq_storage_memory_files() == 0);
    s_fail_writes = 0;
    fl_cmdlog_close(j);
    /* No journal at all refuses too. */
    ASSERT(fl_cmdlog_attempt(NULL, WHO, "dir", &corr, why, sizeof why) == FL_CMDLOG_REFUSE);
    ASSERT(why[0] != '\0' && strstr(why, "log") != NULL);
    return 0;
}

static int test_a_failed_completion_write_is_outcome_unknown(void) {
    fl_cmdlog_config_t c = config(&s_failing, NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    fl_cmdlog_corr_t corr;
    char why[FL_CMDLOG_REASON_MAX];

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    ASSERT(fl_cmdlog_attempt(j, WHO, "write f", &corr, why, sizeof why) == FL_CMDLOG_ALLOW);
    s_fail_writes = 1; /* the command runs; its completion cannot be written */
    ASSERT(fl_cmdlog_complete(j, &corr, 0, why, sizeof why) == -1 && why[0] != '\0');
    fl_cmdlog_print_completion_gap(why);
    s_fail_writes = 0;
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == 0);
    ASSERT(r.records == 1 && r.open_attempts == 1 && memcmp(r.open[0].id, corr.id, sizeof corr.id) == 0);
    return 0;
}

/* A journal of n runs (2n records) through st, with 1 KiB segments (about four records each). */
static int build(const fl_kq_storage_t *st, int n, const fl_cmdlog_limits_t *lim) {
    fl_cmdlog_config_t c = config(st, lim);
    fl_cmdlog_t *j = NULL;

    if (fl_cmdlog_open(&c, &j, NULL) != FL_CMDLOG_OPEN_OK)
        return -1;
    for (int i = 0; i < n; i++)
        if (run_one(j, "listdirs", 0) != 0) {
            fl_cmdlog_close(j);
            return -1;
        }
    fl_cmdlog_close(j);
    return 0;
}

static const fl_cmdlog_limits_t k_small = {0, 1024, 0, 0};

static int test_a_missing_middle_segment_fails_and_is_never_overwritten(void) {
    fl_cmdlog_config_t c = config(&s_failing, &k_small);
    fl_cmdlog_limits_t one = {0, 1024, 0, 1};
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    uint8_t *before = NULL, *after = NULL;
    size_t nb = 0, na = 0;

    fresh();
    ASSERT(build(&s_failing, 12, &k_small) == 0);
    ASSERT(fl_kq_storage_memory_files() >= 4);
    ASSERT(seg_read(2, &before, &nb) == 0);
    s_hidden = DIR_ "/cmdlog-000001.seg"; /* segment 1 is gone; 2 and later remain */
    ASSERT(fl_cmdlog_verify(&s_failing, DIR_, &s_signer, NULL, &k_small, &r) == -1);
    ASSERT(r.status == FL_CMDLOG_V_GAP && r.segments == 1);
    ASSERT(fl_cmdlog_open(&c, &j, &r) == FL_CMDLOG_OPEN_ERROR && j == NULL && r.status == FL_CMDLOG_V_GAP);
    ASSERT(seg_read(2, &after, &na) == 0 && na == nb && memcmp(before, after, nb) == 0); /* untouched */
    s_hidden = NULL;
    /* A journal larger than the retention bound the reader was given is not "OK" either. */
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, &one, &r) == -1 && r.status == FL_CMDLOG_V_GAP);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, &k_small, &r) == 0 && r.records == 24);
    mem_domain_free(MEM_DOMAIN_FS, before);
    mem_domain_free(MEM_DOMAIN_FS, after);
    return 0;
}

static int test_a_partial_record_mid_journal_is_not_a_tail(void) {
    fl_cmdlog_report_t r;
    uint8_t *d = NULL;
    size_t n = 0;

    fresh();
    ASSERT(build(mem(), 6, &k_small) == 0 && fl_kq_storage_memory_files() >= 2);
    ASSERT(seg_read(0, &d, &n) == 0 && seg_write(0, d, n - 10) == 0);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, &k_small, &r) == -1);
    ASSERT(r.status == FL_CMDLOG_V_GAP);
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

static int test_a_torn_tail_is_dropped_across_a_segment_roll(void) {
    fl_cmdlog_config_t c = config(mem(), &k_small);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    uint8_t *d = NULL;
    size_t n = 0;

    fresh();
    ASSERT(build(mem(), 1, &k_small) == 0);
    ASSERT(seg_read(0, &d, &n) == 0 && seg_write(0, d, n - 10) == 0);
    ASSERT(fl_cmdlog_open(&c, &j, &r) == FL_CMDLOG_OPEN_OK && r.tail_incomplete == 1);
    for (int i = 0; i < 10; i++)
        ASSERT(run_one(j, "du", 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, &k_small, &r) == 0);
    ASSERT(!r.tail_incomplete && r.segments >= 3 && r.records == 21 && r.open_attempts == 1);
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

/* A backend that is not atomic: a failed write leaves half a record behind. */
static int test_a_failed_partial_write_is_rewritten(void) {
    fl_cmdlog_config_t c = config(&s_failing, &k_small);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    fl_cmdlog_corr_t corr;
    char why[FL_CMDLOG_REASON_MAX];

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == FL_CMDLOG_OPEN_OK);
    s_partial_writes = 1; /* the very first segment: a new segment index left dirty */
    ASSERT(fl_cmdlog_attempt(j, WHO, "dir", &corr, why, sizeof why) == FL_CMDLOG_REFUSE);
    s_partial_writes = 0;
    ASSERT(run_one(j, "dir", 0) == 0);
    s_partial_writes = 1; /* the active segment now ends in half a record */
    ASSERT(fl_cmdlog_attempt(j, WHO, "dir", &corr, why, sizeof why) == FL_CMDLOG_REFUSE);
    s_partial_writes = 0;
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, &k_small, &r) == 0 && r.tail_incomplete == 1);
    for (int i = 0; i < 10; i++) /* rolls to new segments: the half record must not stay behind */
        ASSERT(run_one(j, "dir", 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, &k_small, &r) == 0);
    ASSERT(!r.tail_incomplete && r.segments >= 3 && r.records == 22 && r.open_attempts == 0);
    return 0;
}

static int test_another_journal_device_or_key_is_foreign(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    uint8_t other[FL_CMDLOG_ID_LEN], *d = NULL;
    size_t n = 0;

    fresh();
    ASSERT(build(mem(), 2, NULL) == 0);
    /* Key: a second key the verifier knows can neither extend nor pass as this journal. */
    c.signer = &s_signer_b;
    ASSERT(fl_cmdlog_open(&c, &j, &r) == FL_CMDLOG_OPEN_ERROR && j == NULL && r.status == FL_CMDLOG_V_FOREIGN);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer_b, NULL, NULL, &r) == -1 && r.status == FL_CMDLOG_V_FOREIGN);
    /* Device. */
    c = config(mem(), NULL);
    memset(c.device_id, 0xd2, sizeof c.device_id);
    memset(other, 0xd2, sizeof other);
    ASSERT(fl_cmdlog_open(&c, &j, &r) == FL_CMDLOG_OPEN_ERROR && r.status == FL_CMDLOG_V_FOREIGN);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, other, NULL, &r) == -1 && r.status == FL_CMDLOG_V_FOREIGN);
    memset(other, 0xd1, sizeof other);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, other, NULL, &r) == 0 && r.records == 4);
    /* Journal id: another journal's (validly signed, same key and device) segment spliced in. */
    c = config(mem(), NULL);
    c.dir = "/other";
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == FL_CMDLOG_OPEN_OK && run_one(j, "dir", 0) == 0);
    fl_cmdlog_close(j);
    ASSERT(mem()->read_file(NULL, "/other/cmdlog-000000.seg", 1u << 20, &d, &n) == 0);
    ASSERT(seg_write(1, d, n) == 0);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == -1 && r.status == FL_CMDLOG_V_FOREIGN);
    ASSERT(r.records == 4);
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

/* A record re-signed with the right key but a wrong prev_hash: the chain, not the signature, fails. */
static int test_a_broken_chain_is_detected(void) {
    fl_cmdlog_report_t r;
    uint8_t *d = NULL;
    size_t n = 0, off[16], len, sig_at;

    fresh();
    ASSERT(build(mem(), 2, NULL) == 0);
    ASSERT(seg_read(0, &d, &n) == 0 && bounds(d, n, off, 16) == 4);
    len = off[3] - off[2];
    sig_at = off[2] + len - 32u; /* the test signer's 32-byte MAC, after its sig_len byte */
    ASSERT(d[sig_at - 1u] == 32);
    d[off[2] + 70] ^= 0x01; /* prev_hash starts at byte 70 of a record */
    ASSERT(hmac_domain(s_keys[0].key, FL_CMDLOG_DOMAIN, d + off[2] + 4, len - 4u - 33u, d + sig_at) == 0);
    ASSERT(seg_write(0, d, n) == 0);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == -1);
    ASSERT(r.status == FL_CMDLOG_V_BROKEN_CHAIN && r.records == 2);
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

static int test_a_second_open_of_one_journal_is_busy(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL, *k = NULL;

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == FL_CMDLOG_OPEN_OK);
    ASSERT(fl_cmdlog_open(&c, &k, NULL) == FL_CMDLOG_OPEN_BUSY && k == NULL);
    c.dir = DIR_ "/";
    ASSERT(fl_cmdlog_open(&c, &k, NULL) == FL_CMDLOG_OPEN_BUSY && k == NULL);
    c.dir = "/elsewhere";
    ASSERT(fl_cmdlog_open(&c, &k, NULL) == FL_CMDLOG_OPEN_OK);
    fl_cmdlog_close(k);
    ASSERT(run_one(j, "dir", 0) == 0);
    fl_cmdlog_close(j);
    c.dir = DIR_;
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == FL_CMDLOG_OPEN_OK); /* free again after close */
    fl_cmdlog_close(j);
    return 0;
}

static int test_category_edges(void) {
    char line[600], cat[FL_CMDLOG_CATEGORY_MAX], s[33];

    ASSERT(kq_test_random_hex(s, 16) == 0);
    memset(line, ' ', 300);
    snprintf(line + 300, sizeof line - 300, "passwd %s", s); /* the name is past the scan limit */
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "other") == 0);
    memset(line, 'a', 300);
    line[300] = '\0';
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "other") == 0);
    snprintf(line, sizeof line, "passwd\t%s", s);
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "passwd") == 0);
    snprintf(line, sizeof line, "\"passwd\" %s", s);
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "passwd") == 0);
    snprintf(line, sizeof line, "history");
    ASSERT(strcmp(fl_cmdlog_category(line, cat, sizeof cat), "history") == 0);
    return 0;
}

static int test_the_refusal_text_names_the_log_and_reason(void) {
    char buf[512] = {0};
    FILE *tmp = tmpfile();
    int saved;
    size_t n;

    ASSERT(tmp != NULL);
    fflush(stderr);
    saved = dup(STDERR_FILENO);
    ASSERT(saved >= 0 && dup2(fileno(tmp), STDERR_FILENO) >= 0);
    fl_color_set_disabled(1);
    fl_cmdlog_print_refusal("command log storage write failed");
    fl_cmdlog_print_completion_gap("command log storage write failed");
    fflush(stderr);
    ASSERT(dup2(saved, STDERR_FILENO) >= 0);
    close(saved);
    rewind(tmp);
    n = fread(buf, 1, sizeof buf - 1, tmp);
    fclose(tmp);
    buf[n] = '\0';
    ASSERT(strstr(buf, "[ERROR] command refused: the signed command log could not be written "
                       "(command log storage write failed)") != NULL);
    ASSERT(strstr(buf, "[WARNING] the command ran, but its completion was not written") != NULL);
    return 0;
}

#define WRITERS 8
#define PER_WRITER 50

static void *writer(void *arg) {
    fl_cmdlog_t *j = (fl_cmdlog_t *)arg;
    intptr_t bad = 0;

    for (int i = 0; i < PER_WRITER; i++)
        if (run_one(j, (i & 1) ? "cat f" : "ping 10.0.0.1", 0) != 0)
            bad++;
    return (void *)bad;
}

static int test_concurrent_writers_keep_one_chain(void) {
    fl_cmdlog_config_t c = config(mem(), NULL);
    fl_cmdlog_t *j = NULL;
    fl_cmdlog_report_t r;
    pthread_t t[WRITERS];

    fresh();
    ASSERT(fl_cmdlog_open(&c, &j, NULL) == 0);
    for (int i = 0; i < WRITERS; i++)
        ASSERT(pthread_create(&t[i], NULL, writer, j) == 0);
    for (int i = 0; i < WRITERS; i++) {
        void *bad = NULL;

        ASSERT(pthread_join(t[i], &bad) == 0 && bad == NULL);
    }
    fl_cmdlog_close(j);
    ASSERT(fl_cmdlog_verify(mem(), DIR_, &s_signer, NULL, NULL, &r) == 0);
    ASSERT(r.records == 2u * WRITERS * PER_WRITER && r.open_attempts == 0 && r.orphan_closes == 0);
    ASSERT(r.segments >= 2); /* the run crossed a segment boundary */
    return 0;
}

int main(void) {
    int rc = 0;

    if (fl_random_bytes(s_keys[0].key, 32) != 0 || fl_random_bytes(s_keys[1].key, 32) != 0) {
        fprintf(stderr, "no entropy for the test signer key\n");
        return 1;
    }
#define RUN(t) do { if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } else printf("%s... OK\n", #t); } while (0)
    RUN(test_a_good_chain_verifies_and_reopens);
    RUN(test_a_tampered_record_is_detected);
    RUN(test_a_reordered_record_is_detected);
    RUN(test_a_truncated_tail_is_reported_and_recovered);
    RUN(test_an_attempt_without_completion_is_outcome_unknown);
    RUN(test_secret_arguments_never_reach_storage);
    RUN(test_bounds_are_enforced);
    RUN(test_a_failed_attempt_write_refuses);
    RUN(test_a_failed_completion_write_is_outcome_unknown);
    RUN(test_concurrent_writers_keep_one_chain);
    RUN(test_a_missing_middle_segment_fails_and_is_never_overwritten);
    RUN(test_a_partial_record_mid_journal_is_not_a_tail);
    RUN(test_a_torn_tail_is_dropped_across_a_segment_roll);
    RUN(test_a_failed_partial_write_is_rewritten);
    RUN(test_another_journal_device_or_key_is_foreign);
    RUN(test_a_broken_chain_is_detected);
    RUN(test_a_second_open_of_one_journal_is_busy);
    RUN(test_category_edges);
    RUN(test_the_refusal_text_names_the_log_and_reason);
    fl_kq_storage_memory_reset();
    mem_domain_secure_zero(s_keys, sizeof s_keys);
    if (rc == 0)
        printf("All command log tests passed.\n");
    return rc;
}
