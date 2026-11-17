#ifndef FL_CMD_JOURNAL_H
#define FL_CMD_JOURNAL_H
/* Hosted only; fails closed. Signature covers bytes after record_len to sig_len (layout: docs/COMMAND_LOG.md). */

#include "kq_storage.h"

#include <stddef.h>
#include <stdint.h>

#define FL_CMDLOG_FORMAT_VERSION 1u
#define FL_CMDLOG_DOMAIN "flinstone.cmdlog.v1"

#define FL_CMDLOG_ID_LEN 16u
#define FL_CMDLOG_HASH_LEN 32u
#define FL_CMDLOG_SIG_MAX 64u
#define FL_CMDLOG_LABEL_MAX 64u
#define FL_CMDLOG_CATEGORY_MAX 32u
#define FL_CMDLOG_REASON_MAX 128u

#define FL_CMDLOG_RECORD_MAX 1024u
#define FL_CMDLOG_SEGMENT_MAX_DEFAULT (64u * 1024u)
#define FL_CMDLOG_TOTAL_MAX_DEFAULT (4u * 1024u * 1024u)
#define FL_CMDLOG_SEGMENTS_MAX_DEFAULT 64u
#define FL_CMDLOG_SEGMENTS_HARD_MAX 1024u
#define FL_CMDLOG_REPORT_OPEN_MAX 32u

typedef enum {
    FL_CMDLOG_EV_ATTEMPT = 1,
    FL_CMDLOG_EV_COMPLETION = 2,
    FL_CMDLOG_EV_REFUSAL = 3,
} fl_cmdlog_event_t;

enum { FL_CMDLOG_TIME_LOCAL = 0, FL_CMDLOG_TIME_TRUSTED = 1 };

typedef struct {
    void *ctx;
    const char *key_id;
    int (*sign)(void *ctx, const char *domain, const uint8_t *msg, size_t len, uint8_t *sig_out,
                size_t *sig_len);
    int (*verify)(void *ctx, const char *key_id, const char *domain, const uint8_t *msg, size_t len,
                  const uint8_t *sig, size_t sig_len);
} fl_cmdlog_signer_t;

typedef struct {
    size_t record_max;
    size_t segment_max;
    size_t total_max;
    unsigned segments_max;
} fl_cmdlog_limits_t;

typedef struct {
    const fl_kq_storage_t *storage;
    const char *dir;
    const fl_cmdlog_signer_t *signer;
    uint8_t device_id[FL_CMDLOG_ID_LEN];
    fl_cmdlog_limits_t limits;
} fl_cmdlog_config_t;

typedef struct {
    uint8_t id[FL_CMDLOG_ID_LEN];
} fl_cmdlog_corr_t;

typedef enum {
    FL_CMDLOG_V_OK = 0,
    FL_CMDLOG_V_MALFORMED,
    FL_CMDLOG_V_BAD_SIGNATURE,
    FL_CMDLOG_V_BROKEN_CHAIN,
    FL_CMDLOG_V_BAD_SEQUENCE,
    FL_CMDLOG_V_FOREIGN,
    FL_CMDLOG_V_IO,
    FL_CMDLOG_V_GAP,
} fl_cmdlog_verdict_code_t;

typedef struct {
    fl_cmdlog_verdict_code_t status;
    uint64_t records;
    uint64_t bad_seq;
    unsigned segments;
    size_t total_bytes;
    int tail_incomplete;
    size_t tail_bytes;
    unsigned open_attempts;
    fl_cmdlog_corr_t open[FL_CMDLOG_REPORT_OPEN_MAX];
    unsigned orphan_closes;
    unsigned cosigs_unverified;
    uint8_t journal_id[FL_CMDLOG_ID_LEN];
    uint8_t device_id[FL_CMDLOG_ID_LEN];
    uint64_t next_seq;
    uint8_t last_hash[FL_CMDLOG_HASH_LEN];
} fl_cmdlog_report_t;

typedef enum { FL_CMDLOG_ALLOW = 0, FL_CMDLOG_REFUSE = 1 } fl_cmdlog_decision_t;

typedef struct fl_cmdlog fl_cmdlog_t;

int fl_cmdlog_verify(const fl_kq_storage_t *storage, const char *dir, const fl_cmdlog_signer_t *signer,
                     const uint8_t *expect_device, const fl_cmdlog_limits_t *limits,
                     fl_cmdlog_report_t *report);

enum { FL_CMDLOG_OPEN_OK = 0, FL_CMDLOG_OPEN_ERROR = -1, FL_CMDLOG_OPEN_BUSY = -2 };

int fl_cmdlog_open(const fl_cmdlog_config_t *cfg, fl_cmdlog_t **out, fl_cmdlog_report_t *report);
void fl_cmdlog_close(fl_cmdlog_t *j);

const char *fl_cmdlog_category(const char *line, char *out, size_t cap);

/* ALLOW: *corr names the record. REFUSE: the command must not run. */
fl_cmdlog_decision_t fl_cmdlog_attempt(fl_cmdlog_t *j, const char *principal, const char *line,
                                       fl_cmdlog_corr_t *corr, char *reason, size_t reason_cap);

int fl_cmdlog_complete(fl_cmdlog_t *j, const fl_cmdlog_corr_t *corr, int rc, char *reason,
                       size_t reason_cap);

int fl_cmdlog_refused(fl_cmdlog_t *j, const fl_cmdlog_corr_t *corr, int code, char *reason,
                      size_t reason_cap);

void fl_cmdlog_print_refusal(const char *reason);
void fl_cmdlog_print_completion_gap(const char *reason);

#endif
