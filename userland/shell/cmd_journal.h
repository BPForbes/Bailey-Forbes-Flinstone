#ifndef FL_CMD_JOURNAL_H
#define FL_CMD_JOURNAL_H

/*
 * Signed command log (FL-04), hosted only: a separate, versioned, append-only journal of what the
 * shell was asked to run. Every record is signed through a signer interface (KeyQuorum later) and
 * carries the SHA-256 of the record before it, so a reader can check order and integrity. See
 * docs/COMMAND_LOG.md.
 *
 * What is recorded: a CATEGORY of the command, never its arguments. The category is the first word
 * of the line when it names a known shell command (the command registry, fl_shell_cmd_lookup, plus
 * the line-mode builtins), and "other" for anything else. Arguments, and so passwords typed on the
 * line, never reach the journal.
 *
 * Record (one per event, big-endian, length-delimited; built and read with net_codec.h). The signature covers every byte after
 * record_len up to (not including) sig_len; record_len is implied by the content and a reader
 * requires the parsed fields to fill it exactly, and the record hash covers all of it:
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
 *
 * Storage: bounded segment replacement over fl_kq_storage_t (kernel/core/vfs/kq_storage.h). The
 * table has no append, rename or durability call, so each append rewrites the active segment
 * "<dir>/cmdlog-NNNNNN.seg" whole with write_atomic. On the placement layer (fl_kq_storage_default)
 * that is a staged write, fsync of the staged file, then rename over the old segment; on the
 * in-memory table the old bytes are replaced only once the new ones are held. Crash recovery: a
 * crash leaves either the old or the new segment, and fl_cmdlog_open re-reads every segment,
 * verifies it and continues the chain; a torn last record (a backend that is not atomic, or a
 * failed write) is reported as an incomplete tail and dropped from the active segment on the next
 * append (or rewritten before the next segment starts). A partial record anywhere but the end of
 * the last segment, a missing segment before a later one, or a segment past the retention bound
 * is FL_CMDLOG_V_GAP / MALFORMED, never a tail. THIS IS NOT PROVEN POWER-LOSS DURABLE: no backend
 * in the tree fsyncs the directory after the rename (write_atomic_at in server_shared_fs.c fsyncs
 * the file only, and the facade uses the same call), and nothing here has been tested against a
 * real power cut.
 *
 * Retention: segments are never pruned, because the table has no remove call and dropping the
 * oldest segment would cut the chain without a signed anchor. When a bound (record, segment count,
 * total bytes) would be passed the append fails, and the failure policy below applies (fail
 * closed). Operator recovery (docs/COMMAND_LOG.md): move the journal directory aside as evidence and
 * start a new journal; a signed rotation anchor is future work.
 *
 * Failure policy. Owner decision (Nov 16): refuse with a red error giving the reason. When the
 * ATTEMPT record cannot be written, fl_cmdlog_attempt returns FL_CMDLOG_REFUSE and a short reason;
 * the caller prints it with fl_cmdlog_print_refusal (fl_color_error) and does not run the command.
 * A COMPLETION write failure cannot undo a command that already ran: fl_cmdlog_complete returns -1
 * and a reason, the caller warns with fl_cmdlog_print_completion_gap, and recovery reports that
 * attempt as "outcome unknown".
 *
 * Binding: a journal belongs to one device id and one signer key id. Open and verify check every
 * record against both (and the first record's journal id); anything else is FL_CMDLOG_V_FOREIGN and
 * the journal is not opened, so a second key the verifier knows cannot extend it.
 *
 * Concurrency: writers through one handle are serialized by its mutex, so threads sharing it keep
 * one chain. A second fl_cmdlog_open of the same (storage table, directory) in this process is
 * refused (FL_CMDLOG_OPEN_BUSY). There is NO cross-process lock (the table has no lock call): each
 * process or session must use its own directory. Close a handle only after every caller is done.
 * Lockout: a journal that does not verify is never reopened, so with the refuse policy every command
 * is refused until an operator moves the directory aside (docs/COMMAND_LOG.md).
 *
 * Not wired in yet: the interpreter hook (userland/shell/interpreter.c) is added later.
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

/*
 * Who signs. KeyQuorum has no generic, domain-separated single-signer C API yet.
 * TODO(FL-04): add a KeyQuorum call that signs and verifies (domain, message) with an account's key
 * (e.g. kq_sign_domain / kq_verify_domain in keyquorum_identity.h) and its driver wrapper in
 * kernel/drivers/keyquorum/kq_driver.c (two ops appended to the keyquorum driver table), then
 * provide a signer over them. Until then tests use a test-only signer.
 *
 * sign: writes the signature of msg under domain into sig_out (cap FL_CMDLOG_SIG_MAX) and its
 * length into *sig_len; 0 on success. verify: 1 when sig is key_id's valid signature of msg under
 * domain, 0 when not, negative when it cannot tell. key_id: the signer's id, recorded in each
 * record (at most FL_CMDLOG_LABEL_MAX - 1 bytes).
 */
typedef struct {
    void *ctx;
    const char *key_id;
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
 * Walks every segment in order and checks the format, the signing domain, that every record is
 * signed by signer->key_id and verifies, one journal and device id throughout (expect_device, when
 * not NULL, must match), the sequence and the hash chain, and that no segment lies past where the
 * walk stopped; reports an incomplete tail and every unclosed ATTEMPT. Returns 0 when
 * report->status is FL_CMDLOG_V_OK (an incomplete tail or unclosed attempts are reported, not
 * failures), -1 otherwise.
 */
int fl_cmdlog_verify(const fl_kq_storage_t *storage, const char *dir, const fl_cmdlog_signer_t *signer,
                     const uint8_t *expect_device, const fl_cmdlog_limits_t *limits,
                     fl_cmdlog_report_t *report);

enum { FL_CMDLOG_OPEN_OK = 0, FL_CMDLOG_OPEN_ERROR = -1, FL_CMDLOG_OPEN_BUSY = -2 };

/*
 * Opens (or starts) the journal in cfg->dir: verifies what is there, against cfg->device_id and the
 * signer's key id, and continues its chain. FL_CMDLOG_OPEN_OK; FL_CMDLOG_OPEN_ERROR when the journal
 * does not verify or belongs to another device or key (it is evidence, and appending would hide it;
 * report->status says why) or on a bad config; FL_CMDLOG_OPEN_BUSY when this process already has the
 * same (storage, dir) open. report (may be NULL) receives the recovery report.
 */
int fl_cmdlog_open(const fl_cmdlog_config_t *cfg, fl_cmdlog_t **out, fl_cmdlog_report_t *report);
/* Only after every caller using j has returned. */
void fl_cmdlog_close(fl_cmdlog_t *j);

/*
 * The redacted category of a command line: "other" unless the first word is a known command. Never
 * copies anything past the first word. Returns out.
 */
const char *fl_cmdlog_category(const char *line, char *out, size_t cap);

/*
 * Records an ATTEMPT before a command runs. FL_CMDLOG_ALLOW: the record is signed and stored, and
 * *corr names it for fl_cmdlog_complete. FL_CMDLOG_REFUSE: it could not be (or j is NULL); reason
 * (cap at least FL_CMDLOG_REASON_MAX recommended) says why, and the command must not run.
 */
fl_cmdlog_decision_t fl_cmdlog_attempt(fl_cmdlog_t *j, const char *principal, const char *line,
                                       fl_cmdlog_corr_t *corr, char *reason, size_t reason_cap);

/* Records the COMPLETION of corr with the command's rc. 0, or -1 with a reason (outcome unknown). */
int fl_cmdlog_complete(fl_cmdlog_t *j, const fl_cmdlog_corr_t *corr, int rc, char *reason,
                       size_t reason_cap);

/* Records that corr was refused after its attempt (e.g. authorization) with refusal code code. */
int fl_cmdlog_refused(fl_cmdlog_t *j, const fl_cmdlog_corr_t *corr, int code, char *reason,
                      size_t reason_cap);

/* The caller's messages: red "[ERROR] command refused: ..." (fl_color_error) and a yellow
 * "[WARNING] ..." completion-gap warning (fl_color_warn_local), both on stderr. */
void fl_cmdlog_print_refusal(const char *reason);
void fl_cmdlog_print_completion_gap(const char *reason);

/*
 * Future per-command fields: an allowlist of (command, field) pairs that may add more than the
 * category. Empty now; fl_cmdlog_allowed_field_count() returns 0 and no field is recorded.
 */
typedef struct {
    const char *command;
    const char *field;
} fl_cmdlog_field_allow_t;
size_t fl_cmdlog_allowed_field_count(void);

#endif /* FL_CMD_JOURNAL_H */
