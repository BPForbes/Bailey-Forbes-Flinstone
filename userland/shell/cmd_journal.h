#ifndef FL_CMD_JOURNAL_H
#define FL_CMD_JOURNAL_H

/*
 * Signed, hash-chained, append-only command journal (hosted only). Records a command CATEGORY, never
 * its arguments. Fails closed: no ATTEMPT record, no command. See docs/COMMAND_LOG.md.
 */

/*
 * Record layout (big-endian, length-delimited). The signature covers the bytes after record_len up to
 * sig_len; the record hash covers the whole record, and parsed fields must fill record_len exactly.
 *
 *   u32  record_len            whole record, this field included
 *   u8   magic[4]              "FLCL"
 *   u16  format_version        FL_CMDLOG_FORMAT_VERSION
 *   u8   domain_len, domain    signing-domain string, FL_CMDLOG_DOMAIN (strings: no NUL inside)
 *   u8   journal_id[16]        random, fixed for a journal's life
 *   u8   device_id[16]         session / device the journal belongs to
 *   u64  seq                   0, 1, 2 ... with no gap, across segments
 *   u8   prev_hash[32]         SHA-256 of the whole previous record (zeros for seq 0)
 *   u8   event_id[16]          random
 *   u8   key_id_len, key_id    the signer's key id: one key per journal
 *   u8   principal_len, label  the account the command ran as
 *   u8   event_type            fl_cmdlog_event_t
 *   u8   category_len, cat     redacted command category
 *   i32  result_code           COMPLETION: the command's rc; REFUSAL: the refusal code; else 0
 *   i64  timestamp_sec         wall clock (fl_time_wall_sec)
 *   u8   time_trust            FL_CMDLOG_TIME_LOCAL (no trusted time source yet); > 1 is malformed
 *   u8   correlation_id[16]    ATTEMPT: its own event_id; COMPLETION/REFUSAL: the attempt's
 *   u8   cosig_present         0 now; 1 adds nonce[16], u8 len + second signer id,
 *                              u8 len + second signature (room for the sudo co-signature)
 *   u8   sig_len, signature
 */

#include "kq_storage.h"

#include <stddef.h>
#include <stdint.h>

#define FL_CMDLOG_FORMAT_VERSION 1u
#define FL_CMDLOG_DOMAIN "flinstone.cmdlog.v1"

#define FL_CMDLOG_ID_LEN 16u
#define FL_CMDLOG_HASH_LEN 32u
#define FL_CMDLOG_SIG_MAX 64u        /* an Ed25519 signature fits */
#define FL_CMDLOG_LABEL_MAX 64u      /* key id, principal, second signer id */
#define FL_CMDLOG_CATEGORY_MAX 32u
#define FL_CMDLOG_REASON_MAX 128u    /* a refusal / gap reason, with its NUL */

/* Default bounds; a config may lower them (never raise past the record maximum). */
#define FL_CMDLOG_RECORD_MAX 1024u
#define FL_CMDLOG_SEGMENT_MAX_DEFAULT (64u * 1024u)
#define FL_CMDLOG_TOTAL_MAX_DEFAULT (4u * 1024u * 1024u)
#define FL_CMDLOG_SEGMENTS_MAX_DEFAULT 64u
/* Segment indices are probed up to this bound for gaps; retention is clamped to it. */
#define FL_CMDLOG_SEGMENTS_HARD_MAX 1024u
/* How many unclosed attempts a report lists (the count is always exact). */
#define FL_CMDLOG_REPORT_OPEN_MAX 32u

typedef enum {
    FL_CMDLOG_EV_ATTEMPT = 1,
    FL_CMDLOG_EV_COMPLETION = 2,
    FL_CMDLOG_EV_REFUSAL = 3,
} fl_cmdlog_event_t;

enum { FL_CMDLOG_TIME_LOCAL = 0, FL_CMDLOG_TIME_TRUSTED = 1 };

/* Signer interface. sign returns 0 on success; verify returns 1 valid, 0 invalid, negative unknown. */
typedef struct {
    void *ctx;
    const char *key_id; /* recorded in each record; at most FL_CMDLOG_LABEL_MAX - 1 bytes */
    int (*sign)(void *ctx, const char *domain, const uint8_t *msg, size_t len, uint8_t *sig_out,
                size_t *sig_len);
    int (*verify)(void *ctx, const char *key_id, const char *domain, const uint8_t *msg, size_t len,
                  const uint8_t *sig, size_t sig_len);
} fl_cmdlog_signer_t;

typedef struct {
    size_t record_max;    /* 0: FL_CMDLOG_RECORD_MAX */
    size_t segment_max;   /* 0: FL_CMDLOG_SEGMENT_MAX_DEFAULT */
    size_t total_max;     /* 0: FL_CMDLOG_TOTAL_MAX_DEFAULT */
    unsigned segments_max; /* retention: 0: FL_CMDLOG_SEGMENTS_MAX_DEFAULT */
} fl_cmdlog_limits_t;

typedef struct {
    const fl_kq_storage_t *storage; /* required; the shell passes fl_kq_cp_storage() */
    const char *dir;                /* segment directory (no trailing '/' needed) */
    const fl_cmdlog_signer_t *signer;
    uint8_t device_id[FL_CMDLOG_ID_LEN];
    fl_cmdlog_limits_t limits;
} fl_cmdlog_config_t;

typedef struct {
    uint8_t id[FL_CMDLOG_ID_LEN];
} fl_cmdlog_corr_t;

typedef enum {
    FL_CMDLOG_V_OK = 0,
    FL_CMDLOG_V_MALFORMED,    /* a record does not parse (not at the tail), or a bad domain/version */
    FL_CMDLOG_V_BAD_SIGNATURE,
    FL_CMDLOG_V_BROKEN_CHAIN, /* prev_hash does not match the record before */
    FL_CMDLOG_V_BAD_SEQUENCE, /* a gap, a repeat or a reorder */
    FL_CMDLOG_V_FOREIGN,      /* another journal id, device id or signer key id */
    FL_CMDLOG_V_IO,           /* a segment could not be read (or a hash failed) */
    FL_CMDLOG_V_GAP,          /* a segment exists after a missing one, after a partial record, or
                                 past the retention bound */
} fl_cmdlog_verdict_code_t;

typedef struct {
    fl_cmdlog_verdict_code_t status;
    uint64_t records;            /* records that verified, in order */
    uint64_t bad_seq;            /* the sequence number where verification stopped */
    unsigned segments;
    size_t total_bytes;
    int tail_incomplete;         /* the last segment ends in a partial record */
    size_t tail_bytes;
    unsigned open_attempts;      /* ATTEMPTs with no COMPLETION or REFUSAL: outcome unknown */
    fl_cmdlog_corr_t open[FL_CMDLOG_REPORT_OPEN_MAX];
    unsigned orphan_closes;      /* a COMPLETION/REFUSAL whose attempt is not in the journal */
    unsigned cosigs_unverified;  /* co-signature material present but not checked yet */
    uint8_t journal_id[FL_CMDLOG_ID_LEN];
    uint8_t device_id[FL_CMDLOG_ID_LEN];
    uint64_t next_seq;
    uint8_t last_hash[FL_CMDLOG_HASH_LEN];
} fl_cmdlog_report_t;

typedef enum { FL_CMDLOG_ALLOW = 0, FL_CMDLOG_REFUSE = 1 } fl_cmdlog_decision_t;

typedef struct fl_cmdlog fl_cmdlog_t;

/*
 * Verifies every segment in order: format, domain, signatures, ids, sequence and hash chain.
 * Returns 0 when report->status is FL_CMDLOG_V_OK (an incomplete tail or unclosed attempts are
 * reported, not failures), -1 otherwise. expect_device may be NULL.
 */
int fl_cmdlog_verify(const fl_kq_storage_t *storage, const char *dir, const fl_cmdlog_signer_t *signer,
                     const uint8_t *expect_device, const fl_cmdlog_limits_t *limits,
                     fl_cmdlog_report_t *report);

enum { FL_CMDLOG_OPEN_OK = 0, FL_CMDLOG_OPEN_ERROR = -1, FL_CMDLOG_OPEN_BUSY = -2 };

/*
 * Opens or starts the journal in cfg->dir after verifying it. Returns OPEN_OK, OPEN_ERROR when the
 * journal does not verify, is foreign or the config is bad (nothing is appended), OPEN_BUSY when
 * this process already has the same (storage, dir) open. report may be NULL.
 */
int fl_cmdlog_open(const fl_cmdlog_config_t *cfg, fl_cmdlog_t **out, fl_cmdlog_report_t *report);
/* Only after every caller using j has returned. */
void fl_cmdlog_close(fl_cmdlog_t *j);

/* Writes the redacted category of line to out ("other" unless the first word is a known command). */
const char *fl_cmdlog_category(const char *line, char *out, size_t cap);

/*
 * Records an ATTEMPT before a command runs. Returns ALLOW with *corr set, or REFUSE with a reason
 * (also when j is NULL): the command must not run.
 */
fl_cmdlog_decision_t fl_cmdlog_attempt(fl_cmdlog_t *j, const char *principal, const char *line,
                                       fl_cmdlog_corr_t *corr, char *reason, size_t reason_cap);

/* Records the COMPLETION of corr with rc. Returns 0, or -1 with a reason (outcome unknown). */
int fl_cmdlog_complete(fl_cmdlog_t *j, const fl_cmdlog_corr_t *corr, int rc, char *reason,
                       size_t reason_cap);

/* Records a REFUSAL of corr with the refusal code. Returns 0, or -1 with a reason. */
int fl_cmdlog_refused(fl_cmdlog_t *j, const fl_cmdlog_corr_t *corr, int code, char *reason,
                      size_t reason_cap);

/* Print the refusal error and the completion-gap warning on stderr. */
void fl_cmdlog_print_refusal(const char *reason);
void fl_cmdlog_print_completion_gap(const char *reason);

#endif /* FL_CMD_JOURNAL_H */
