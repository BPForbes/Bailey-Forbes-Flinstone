#ifndef FL_KQ_SERVICE_H
#define FL_KQ_SERVICE_H

/*
 * The KeyQuorum key service (FL-20, absorbs FL-24): the resident holder of the signing keys of the accounts
 * that are signed in, and the one place periodic and unattended signing runs.
 *
 *  - Online through a login: the account that logs in brings its own key online (it is opened with the
 *    password just verified); `su` without a password brings nothing online, and logout wipes the key.
 *  - Its own thread and queue: a service thread with cancellation disabled and every signal blocked polls a
 *    private workqueue (kernel/core/sched/workqueue.h) whose items are all FL_WQ_PROTECTED, so the sweep that
 *    signs is never dropped when the queue is full (the caller gets BUSY and must retry, never discard) and
 *    runs once more at an orderly shutdown. The thread stamps a heartbeat; fl_kq_service_alive() is false once it
 *    is stale, and the monitor (FL-09) refuses every change while it is false: fail closed.
 *  - The hosted shell is an ordinary process, so the host OS can still kill it; what this guarantees is that
 *    nothing inside the shell can (FL_AUTHZ_OP_KQ_SERVICE_CONTROL is denied for every account) and that no
 *    change goes through unmonitored when the service is not alive.
 *
 * The service holds only the 32-byte signing secret fl_user_db_open_key returns, in a fixed table, wiped on
 * logout, on shutdown and when a slot is reused. Copies handed to a caller are the caller's to wipe.
 */

#include "contract_result.h"
#include "user_db.h"
#include "workqueue.h"

#include <stddef.h>
#include <stdint.h>

/** How the author's key was obtained for a signature (recorded in the history event, KQ-12). */
typedef enum {
    KQ_SIGNING_OWN = 1,       /* the author is signed in and signs with their own key */
    KQ_SIGNING_ASSEMBLED = 2  /* a parent assembled from its children's shares (KQ-18: not available yet) */
} kq_signing_mode_t;

/** A heartbeat older than this is a stalled service. The loop wakes every FL_KQ_SERVICE_TICK_MS. */
#define FL_KQ_SERVICE_TICK_MS 100u
#define FL_KQ_HEARTBEAT_MAX_NS (2000ll * 1000000ll)
/** Objects the periodic sweep can watch. */
#define FL_KQ_AUTOSIGN_MAX 16u

/** Start the service thread and install the session and watchdog hooks. Idempotent. Single-thread call. */
fl_result_t fl_kq_service_start(void);
/** The only way the service ends: stops the thread (protected work runs once), wipes every key. */
void fl_kq_service_shutdown(void);

fl_result_t fl_kq_service_online_for_session(const fl_user_record_t *account, const char *password,
                                             uint64_t login_generation);
fl_result_t fl_kq_service_online_from_login(const char *account, const char *password);
void fl_kq_service_offline(const char *account);
/** Accounts whose key is online. */
size_t fl_kq_service_online_count(void);

/**
 * Last resort when no one signs in: bring the service up with Root's stored credential. FL_RESULT_NOENT when
 * none is stored, which is every build today (where it would live is an open decision), and then the service
 * stays up and unattended work waits.
 */
fl_result_t fl_kq_service_root_fallback(void);

/** A copy of the signing secret to sign as author_key_id, for the caller to wipe. ACCES when that account is
 *  not signed in (nothing can assemble a leaf; a parent needs KQ-18). */
fl_result_t fl_kq_obtain_author_key(const char *author_key_id, uint8_t out[32], kq_signing_mode_t *mode);

/** 1 when the service thread has stamped its heartbeat within FL_KQ_HEARTBEAT_MAX_NS. */
int fl_kq_service_alive(void);
/** Audit a stall once per transition (the watchdog calls it). */
void fl_kq_service_report_stalled(void);

/** Queue protected work on the service queue (the FL_WQ_PROTECTED flag is forced). BUSY: retry, never drop. */
fl_result_t fl_kq_service_submit(const fl_wq_work_t *work);
fl_result_t fl_kq_service_submit_every(const fl_wq_work_t *work, int64_t period_ns);

/**
 * The periodic sweep for one tracked object. The three callbacks are what the monitor (FL-09) and KeyQuorum's
 * head-author query (KQ-20) provide; the job is the same for every object: skip while a signature is recent,
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

/** The service thread, for tests that prove it cannot be cancelled or signalled. */
#include <pthread.h>
pthread_t fl_kq_service_thread_for_test(void);

#endif /* FL_KQ_SERVICE_H */
