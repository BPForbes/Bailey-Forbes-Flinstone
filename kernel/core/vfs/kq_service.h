#ifndef FL_KQ_SERVICE_H
#define FL_KQ_SERVICE_H

/*
 * The KeyQuorum key service: the resident holder of the signing keys of the accounts
 * that are signed in, and the one place periodic and unattended signing runs.
 *
 *  - Online through a login: the account that logs in brings its own key online (it is opened with the
 *    password just verified); `su` without a password brings nothing online, and logout wipes the key.
 *  - Its own thread and queue: a service thread with cancellation disabled and every signal blocked polls a
 *    private workqueue (kernel/core/sched/workqueue.h) whose items are all FL_WQ_PROTECTED, so the sweep that
 *    signs is never dropped when the queue is full (the caller gets BUSY and must retry, never discard) and
 *    runs once more at an orderly shutdown. The thread stamps a heartbeat; fl_kq_service_alive() is false once it
 *    is stale, and the monitor refuses every change while it is false: fail closed.
 *  - The hosted shell is an ordinary process, so the host OS can still kill it; what this guarantees is that
 *    nothing inside the shell can (FL_AUTHZ_OP_KQ_SERVICE_CONTROL is denied for every account) and that no
 *    change goes through unmonitored when the service is not alive.
 *
 * The service holds only the 32-byte signing secret fl_user_db_open_key returns, in a fixed table, wiped on
 * logout, on shutdown and when a slot is reused. Copies handed to a caller are the caller's to wipe.
 */

#include "contract_result.h"
#include "fl/driver/keyquorum.h"
#include "kq_storage.h"
#include "user_db.h"
#include "workqueue.h"

#include <stddef.h>
#include <stdint.h>

/** How the author's key was obtained for a signature (recorded in the history event). */
typedef enum {
    KQ_SIGNING_OWN = 1,       /* the author is signed in and signs with their own key */
    KQ_SIGNING_ASSEMBLED = 2  /* a parent key assembled from its children's shares */
} kq_signing_mode_t;

/** A heartbeat older than this is a stalled service. The loop wakes every FL_KQ_SERVICE_TICK_MS. */
#define FL_KQ_SERVICE_TICK_MS 100u
#define FL_KQ_HEARTBEAT_MAX_NS (2000ll * 1000000ll)
/** Objects the periodic sweep can watch. */
#define FL_KQ_AUTOSIGN_MAX 16u

/** Idempotent; single-thread call. */
fl_result_t fl_kq_service_start(void);
/** Stops the thread (protected work runs once) and wipes every key. */
void fl_kq_service_shutdown(void);

fl_result_t fl_kq_service_online_for_session(const fl_user_record_t *account, const char *password,
                                             uint64_t login_generation);
fl_result_t fl_kq_service_online_from_login(const char *account, const char *password);
/**
 * Publish an already-opened signing secret (KQST login). Same generation rules as
 * online_for_session. Caller wipes secret after return.
 */
fl_result_t fl_kq_service_online_opened(const fl_user_record_t *account, const uint8_t secret[32],
                                        uint64_t login_generation);
/**
 * Keeps the account's box (X25519) secret beside its signing secret until logout. Generation rules as
 * online_opened. Returns ACCES when the account is not online. Caller wipes box_secret after return.
 */
fl_result_t fl_kq_service_online_box(const fl_user_record_t *account, const uint8_t box_secret[32],
                                     uint64_t login_generation);
/** Copies the box secret of the account signed in as author_key_id. Caller wipes out.
 *  Returns ACCES when that account is not signed in or its login opened no box key. */
fl_result_t fl_kq_obtain_box_key(const char *author_key_id, uint8_t out[32]);
void fl_kq_service_offline(const char *account);
size_t fl_kq_service_online_count(void);

/** Copies the signing secret to sign as author_key_id; *mode says how it was obtained. Caller wipes out.
 *  Returns ACCES when the account is not signed in and cannot be assembled from its children online. */
fl_result_t fl_kq_obtain_author_key(const char *author_key_id, uint8_t out[32], kq_signing_mode_t *mode);

int fl_kq_service_alive(void);
/** Returns 1 when the resident key table is locked in RAM (taken at fl_kq_service_start). */
int fl_kq_service_keys_locked(void);
/** Audited once per transition. */
void fl_kq_service_report_stalled(void);

/** FL_WQ_PROTECTED is forced. BUSY: retry, never drop. */
fl_result_t fl_kq_service_submit(const fl_wq_work_t *work);
fl_result_t fl_kq_service_submit_every(const fl_wq_work_t *work, int64_t period_ns);

/**
 * The periodic sweep for one tracked object. The three callbacks are what the monitor and KeyQuorum's
 * head-author query provide; the job is the same for every object: skip while a signature is recent,
 * find the head author (a forked history has none: audited and skipped), obtain that author's key (an author
 * who is not signed in is audited and tried again next period), sign, wipe the copy.
 */
typedef struct {
    const char *label;                                       /* audit subject; copied */
    void *ctx;
    int64_t (*last_signature_sec)(void *ctx);                /* wall seconds of the newest signature */
    fl_result_t (*head_author)(void *ctx, char *out, size_t cap); /* FL_RESULT_BUSY: forked, no head */
    fl_result_t (*sign)(void *ctx, const char *author, const uint8_t key[32], kq_signing_mode_t mode);
    int64_t interval_sec;
} fl_kq_autosign_t;
fl_result_t fl_kq_service_autosign_add(const fl_kq_autosign_t *spec);

/** Context of the tracked-file autosign callbacks; a larger context struct must start with one. */
typedef struct {
    const char *path; /* the tracked file; its container is path + FL_KQ_CONTAINER_SUFFIX */
} fl_kq_autosign_file_t;
/** head_author callback for a tracked file. Returns OK with the label, BUSY when the history is forked. */
fl_result_t fl_kq_autosign_head_author(void *ctx, char *out, size_t cap);

/** last_signature_sec (0 when none or unreadable) and sign callbacks for a tracked file. */
int64_t fl_kq_autosign_last_signature(void *ctx);
fl_result_t fl_kq_autosign_sign(void *ctx, const char *author, const uint8_t key[32], kq_signing_mode_t mode);

/**
 * Keeps the file at path signed every interval_sec by its head author. The path is copied.
 * Idempotent: tracking a path again re-activates it. Returns NOMEM when all FL_KQ_AUTOSIGN_MAX slots
 * are taken (a removed slot stays taken until shutdown).
 */
fl_result_t fl_kq_service_autosign_track_file(const char *path, int64_t interval_sec);

/** Stops signing the object registered under label; runs in flight finish. NOENT when not registered. */
fl_result_t fl_kq_service_autosign_remove(const char *label);

/** Default sweep period for a tracked file. */
#define FL_KQ_AUTOSIGN_DEFAULT_SEC 86400ll

/** Writes the wall-clock time as KeyQuorum's UTC text ("2026-11-10T09:00:00.000Z"). */
void fl_kq_service_utc_now(char *out, size_t cap);

/**
 * Signs a KQER refusal report as the online account and writes it inside the held directory dir.
 * Only a refusal of the letter itself (INVAL, NOENT, ACCES, NOMEM) is reported; a NULL dir writes
 * nothing. The outcome is audited without the report or its ref, and never changes the refusal.
 */
void fl_kq_report_refusal_in(const kq_file_offer_t *o, fl_result_t why, const fl_kq_dir_t *dir);

/** For tests that prove the thread cannot be cancelled or signalled. */
#include <pthread.h>
pthread_t fl_kq_service_thread_for_test(void);

#endif /* FL_KQ_SERVICE_H */
