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
    KQ_SIGNING_ASSEMBLED = 2  /* a parent assembled from its children's shares (KeyQuorum parent-key assembly) */
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
 * Keep the account's box (X25519) secret next to its signing secret for the life of the session (FL-15): what a
 * KQST login opened and used to wipe. Same generation rules as online_opened, and the account must already be
 * online (ACCES otherwise). Wiped with the signing secret at logout. Caller wipes box_secret after return.
 */
fl_result_t fl_kq_service_online_box(const fl_user_record_t *account, const uint8_t box_secret[32],
                                     uint64_t login_generation);
/** A copy of the box secret of the account signed in under key id author_key_id, for the caller to wipe. ACCES
 *  when that account is not signed in or came in by a login that opened no box key. Only that account's own
 *  label has a key: the box key belongs to the label it was minted for. */
fl_result_t fl_kq_obtain_box_key(const char *author_key_id, uint8_t out[32]);
void fl_kq_service_offline(const char *account);
size_t fl_kq_service_online_count(void);

/**
 * Last resort when no one signs in: bring the service up with Root's stored credential. FL_RESULT_NOENT when
 * none is stored, which is every build today (where it would live is an open decision), and then the service
 * stays up and unattended work waits.
 */
fl_result_t fl_kq_service_root_fallback(void);

/**
 * Where Root's credential comes from is each runtime's to say, the way the labs let the runtime supply entropy,
 * the clock, storage and secret input: a hosted build reads a bounded file it is told about, the freestanding
 * image a reserved region, the labs nothing. The provider writes the NUL-terminated credential into out[cap]
 * and returns OK, NOENT when none is stored (the service stays up and unattended work waits), or any other
 * result to refuse. The service verifies the credential opens Root's pinned identity (the same login path as
 * a typed password), wipes its copy at once, and never stores or logs it. NULL removes the provider; with none
 * set the fallback answers NOENT, as before. The credential format, path and boot window are policy this does
 * not decide.
 */
typedef fl_result_t (*fl_kq_root_cred_fn)(char *out, size_t cap);
void fl_kq_service_set_root_credential_provider(fl_kq_root_cred_fn provider);

/** A copy of the signing secret to sign as author_key_id, for the caller to wipe. ACCES when that account is
 *  not signed in and, for a parent whose assembly record the account store holds, cannot be assembled from
 *  the children online now (KQ_SIGNING_ASSEMBLED when it can; audited kq-assembled / kq-assemble-refused).
 *  Nothing can assemble a leaf. */
fl_result_t fl_kq_obtain_author_key(const char *author_key_id, uint8_t out[32], kq_signing_mode_t *mode);

int fl_kq_service_alive(void);
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

/**
 * The production head_author callback for a tracked file: KeyQuorum names the author of the file's
 * single head (fl_kq_cp_head_author over the container beside path). ctx points at an
 * fl_kq_autosign_file_t; a caller whose other two callbacks need more context puts one first in
 * its own struct. OK with the label; FL_RESULT_BUSY when the history is forked (the sweep audits
 * kq-autosign-forked and signs nothing); any other failure (no container, no revision, malformed)
 * is another non-OK result and names nobody.
 */
typedef struct {
    const char *path; /* the tracked file; its container is path + FL_KQ_CONTAINER_SUFFIX */
} fl_kq_autosign_file_t;
fl_result_t fl_kq_autosign_head_author(void *ctx, char *out, size_t cap);

/**
 * The other two production callbacks for a tracked file (ctx an fl_kq_autosign_file_t, as above):
 * the time of the newest signature in the file's history (fl_kq_cp_last_signature; 0 when there is none or
 * the history cannot be read), and the signing call itself (fl_kq_cp_sign_head_with: the file's current bytes
 * are checked in as a new revision signed by the author). A fork answers BUSY.
 */
int64_t fl_kq_autosign_last_signature(void *ctx);
fl_result_t fl_kq_autosign_sign(void *ctx, const char *author, const uint8_t key[32], kq_signing_mode_t mode);

/**
 * Keeps the file at path signed every interval_sec by the head author, using the three callbacks above.
 * The path is copied; the object is owned by the service. Idempotent: tracking a path again re-activates it
 * (a removed object's periodic item cannot be cancelled, so it is reused, never queued twice). NOMEM when
 * all FL_KQ_AUTOSIGN_MAX slots are taken (a removed slot stays taken until shutdown).
 */
fl_result_t fl_kq_service_autosign_track_file(const char *path, int64_t interval_sec);

/** Stops signing the object registered under label (a tracked file's path). Runs already in flight finish
 *  with what they copied; no new run starts. NOENT when nothing is registered under that label. */
fl_result_t fl_kq_service_autosign_remove(const char *label);

/** Default sweep period for a tracked file: a placeholder until the owner sets the policy. */
#define FL_KQ_AUTOSIGN_DEFAULT_SEC 86400ll

/** Now as KeyQuorum's UTC text ("2026-11-10T09:00:00.000Z"), from the wall clock. */
void fl_kq_service_utc_now(char *out, size_t cap);

/**
 * The one refusal report (KQER): signs it as the account that is online and writes it inside dir,
 * the landing directory the decode held (fl_kq_cp_report_write_in; the caller keeps the handle
 * open until this returns). Only a refusal of the letter itself (INVAL, NOENT, ACCES, NOMEM) is
 * reported. NULL dir writes nothing and audits kq-report-uncontained; that is what the declared
 * fl_kq_report_refusal (fl/kq.h) does, since it has no held directory. Audits kq-report,
 * kq-report-exists, kq-report-capped, kq-report-offline or kq-report-failed, never the report or
 * its ref. Reporting never changes the refusal.
 */
void fl_kq_report_refusal_in(const kq_file_offer_t *o, fl_result_t why, const fl_kq_dir_t *dir);

/** For tests that prove the thread cannot be cancelled or signalled. */
#include <pthread.h>
pthread_t fl_kq_service_thread_for_test(void);

#endif /* FL_KQ_SERVICE_H */
