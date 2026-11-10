#include "kq_service.h"

#include "bg_jobs.h"
#include "common.h"
#include "fl/audit_log.h"
#include "fl/authz_subsystem.h"
#include "fl/driver/keyquorum.h"
#include "fl/kq.h"
#include "kq_checkpoint.h"
#include "contract_p2_authz.h"
#include "mem_domain.h"
#include "session.h"
#include "timekeeping.h"
#include "user_db.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

typedef struct {
    char key_id[FL_HERITAGE_KEY_ID_MAX];
    uint8_t secret[FL_USER_SIGNING_SECRET_LEN];
    int used;
} kq_slot_t;

typedef struct {
    int used;
    fl_kq_autosign_t spec;
    char label[96];
} kq_object_t;

static kq_slot_t s_slots[FL_USER_DB_MAX_USERS];
static kq_object_t s_objects[FL_KQ_AUTOSIGN_MAX];
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;   /* the key table and the object table */
static pthread_mutex_t s_wake_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_wake_cv;
static fl_workqueue_storage_t s_wq_store;
static fl_workqueue_t *const s_wq = (fl_workqueue_t *)&s_wq_store;
static pthread_t s_thread;
static int s_started;
static _Atomic int s_stop;
static _Atomic int64_t s_heartbeat_ns;
static _Atomic int s_stalled_reported;


static int slot_find_locked(const char *key_id) {
    for (size_t i = 0; i < FL_USER_DB_MAX_USERS; i++)
        if (s_slots[i].used && strcmp(s_slots[i].key_id, key_id) == 0)
            return (int)i;
    return -1;
}

static void slot_wipe_locked(kq_slot_t *s) {
    mem_domain_secure_zero(s->secret, sizeof s->secret);
    memset(s->key_id, 0, sizeof s->key_id);
    s->used = 0;
}

static fl_result_t slot_put(const char *key_id, const uint8_t secret[FL_USER_SIGNING_SECRET_LEN]) {
    fl_result_t rc = FL_RESULT_NOMEM;
    int i;

    pthread_mutex_lock(&s_mu);
    i = slot_find_locked(key_id);
    if (i < 0)
        for (size_t k = 0; k < FL_USER_DB_MAX_USERS && i < 0; k++)
            if (!s_slots[k].used)
                i = (int)k;
    if (i >= 0) {
        slot_wipe_locked(&s_slots[i]); /* a reused slot is wiped first */
        snprintf(s_slots[i].key_id, sizeof s_slots[i].key_id, "%s", key_id);
        memcpy(s_slots[i].secret, secret, sizeof s_slots[i].secret);
        s_slots[i].used = 1;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_mu);
    return rc;
}

typedef struct {
    const char *key_id;
    const uint8_t *secret;
} slot_put_ctx_t;

static fl_result_t slot_put_cb(void *p) {
    const slot_put_ctx_t *c = (const slot_put_ctx_t *)p;

    return slot_put(c->key_id, c->secret);
}

fl_result_t fl_kq_service_online_for_session(const fl_user_record_t *account, const char *password,
                                             uint64_t login_generation) {
    uint8_t secret[FL_USER_SIGNING_SECRET_LEN];
    fl_result_t rc;

    if (!account || !password)
        return FL_RESULT_INVAL;
    /* The key derivation is slow and runs from the record copy with no lock held. open_key_record wipes
     * secret on failure. Publishing then happens under the session lock, only if the login that this
     * key belongs to is still the current one: a logout that overlapped the derivation wins. */
    rc = fl_user_db_open_key_record(account, password, secret);
    if (rc == FL_RESULT_OK) {
        slot_put_ctx_t put = {account->key_id, secret};

        rc = fl_session_publish_if_current(login_generation, slot_put_cb, &put);
    }
    mem_domain_secure_zero(secret, sizeof secret);
    fl_audit_identity_event(account->key_id,
                            rc == FL_RESULT_OK ? "kq-service-online"
                            : rc == FL_RESULT_BUSY ? "kq-service-stale-login"
                                                   : "kq-service-refused",
                            account->key_id, NULL);
    return rc;
}

fl_result_t fl_kq_service_online_from_login(const char *account, const char *password) {
    fl_user_record_t rec;
    fl_result_t rc;

    if (!account || !password)
        return FL_RESULT_INVAL;
    rc = fl_session_snapshot_account(account, &rec);
    if (rc != FL_RESULT_OK) {
        fl_audit_identity_event(account, "kq-service-refused", account, NULL);
        return rc;
    }
    return fl_kq_service_online_for_session(&rec, password, 0); /* no session login to be stale against */
}

fl_result_t fl_kq_service_online_opened(const fl_user_record_t *account, const uint8_t secret[32],
                                        uint64_t login_generation) {
    fl_result_t rc;
    slot_put_ctx_t put;

    if (!account || !secret)
        return FL_RESULT_INVAL;
    put.key_id = account->key_id;
    put.secret = secret;
    rc = fl_session_publish_if_current(login_generation, slot_put_cb, &put);
    fl_audit_identity_event(account->key_id,
                            rc == FL_RESULT_OK ? "kq-service-online"
                            : rc == FL_RESULT_BUSY ? "kq-service-stale-login"
                                                   : "kq-service-refused",
                            account->key_id, NULL);
    return rc;
}

void fl_kq_service_offline(const char *account) {
    fl_user_record_t rec;
    const char *key_id = account;
    int i;

    if (!account)
        return;
    if (fl_session_snapshot_account(account, &rec) == FL_RESULT_OK)
        key_id = rec.key_id;
    pthread_mutex_lock(&s_mu);
    i = slot_find_locked(key_id);
    if (i >= 0)
        slot_wipe_locked(&s_slots[i]);
    pthread_mutex_unlock(&s_mu);
    if (i >= 0)
        fl_audit_identity_event(key_id, "kq-service-offline", key_id, NULL);
}

size_t fl_kq_service_online_count(void) {
    size_t n = 0;

    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_USER_DB_MAX_USERS; i++)
        n += s_slots[i].used ? 1u : 0u;
    pthread_mutex_unlock(&s_mu);
    return n;
}

/* Where Root's credential would be stored for unattended boot is an open decision (the image region
 * needs a written exception and a threat model); nothing is stored, so there is no fallback. */
static fl_result_t stored_root_credential(char *out, size_t cap) {
    (void)out;
    (void)cap;
    return FL_RESULT_NOENT;
}

fl_result_t fl_kq_service_root_fallback(void) {
    char cred[128];
    fl_result_t rc = stored_root_credential(cred, sizeof cred);

    if (rc != FL_RESULT_OK)
        return FL_RESULT_NOENT;
    rc = fl_kq_service_online_from_login(FL_HERITAGE_ROOT_KEY, cred);
    mem_domain_secure_zero(cred, sizeof cred);
    return rc;
}

fl_result_t fl_kq_obtain_author_key(const char *author_key_id, uint8_t out[32], kq_signing_mode_t *mode) {
    int i;
    fl_result_t rc = FL_RESULT_ACCES;

    if (!author_key_id || !out || !mode)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_mu);
    i = slot_find_locked(author_key_id);
    if (i >= 0) {
        memcpy(out, s_slots[i].secret, FL_USER_SIGNING_SECRET_LEN);
        *mode = KQ_SIGNING_OWN;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_mu);
    /* A leaf who is not signed in cannot be assembled by anything: the answer is ACCES. */
    return rc;
}


static void stamp_heartbeat(void) {
    int64_t now = 0;

    if (fl_time_monotonic_ns(&now) == FL_RESULT_OK) {
        atomic_store(&s_heartbeat_ns, now);
        atomic_store(&s_stalled_reported, 0);
    }
}

static void *service_main(void *arg) {
    sigset_t all;

    (void)arg;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); /* pthread_cancel cannot stop it */
    sigfillset(&all);
    (void)pthread_sigmask(SIG_BLOCK, &all, NULL);               /* ^C reaches only the shell thread */
    while (!atomic_load(&s_stop)) {
        struct timespec dl;

        unsigned ran = fl_wq_poll(s_wq, 8); /* protected work only: this queue takes nothing else */

        stamp_heartbeat();
        if (ran == 8)
            continue; /* more may be waiting: the heartbeat is stamped between batches, then straight on */
        (void)clock_gettime(CLOCK_MONOTONIC, &dl);
        dl.tv_nsec += (long)FL_KQ_SERVICE_TICK_MS * 1000000L;
        if (dl.tv_nsec >= 1000000000L) {
            dl.tv_sec++;
            dl.tv_nsec -= 1000000000L;
        }
        pthread_mutex_lock(&s_wake_mu);
        if (!atomic_load(&s_stop))
            (void)pthread_cond_timedwait(&s_wake_cv, &s_wake_mu, &dl); /* a timeout, not a spin */
        pthread_mutex_unlock(&s_wake_mu);
    }
    return NULL;
}

pthread_t fl_kq_service_thread_for_test(void) {
    return s_thread;
}

int fl_kq_service_alive(void) {
    int64_t now = 0, beat;

    if (!s_started || fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        return 0;
    beat = atomic_load(&s_heartbeat_ns);
    return beat != 0 && now - beat < FL_KQ_HEARTBEAT_MAX_NS;
}

void fl_kq_service_report_stalled(void) {
    if (atomic_exchange(&s_stalled_reported, 1) == 0)
        fl_audit_identity_event("", "kq-service-stalled", "", NULL);
}

static void audit_control_denied(unsigned op) {
    (void)op;
    fl_audit_identity_event(fl_session_current_key_id(), "kq-service-control-denied", "", NULL);
}

fl_result_t fl_kq_service_start(void) {
    pthread_condattr_t ca;

    if (s_started)
        return FL_RESULT_OK;
    /* No executor is set: this queue runs no durable record until one is configured on purpose. */
    if (fl_wq_init(s_wq) != FL_RESULT_OK || pthread_condattr_init(&ca) != 0)
        return FL_RESULT_ERR;
    (void)pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    (void)pthread_cond_init(&s_wake_cv, &ca);
    pthread_condattr_destroy(&ca);
    atomic_store(&s_stop, 0);
    atomic_store(&s_heartbeat_ns, 0);
    stamp_heartbeat();
    if (pthread_create(&s_thread, NULL, service_main, NULL) != 0) {
        pthread_cond_destroy(&s_wake_cv);
        fl_wq_destroy(s_wq);
        return FL_RESULT_ERR;
    }
    s_started = 1;
    fl_session_set_login_hooks(fl_kq_service_online_for_session, fl_kq_service_offline,
                               fl_kq_service_online_opened);
    fl_authz_subsystem_set_deny_audit(audit_control_denied);
    (void)fl_bg_watchdog_register(fl_kq_service_alive, fl_kq_service_report_stalled);
    return FL_RESULT_OK;
}

void fl_kq_service_shutdown(void) {
    if (!s_started)
        return;
    /* Drop the alive() check before the service thread stops: fl_bg_jobs_shutdown still drains the
     * protected watchdog item once, and without this it would see !alive and audit a false stall. */
    fl_bg_watchdog_clear();
    atomic_store(&s_stop, 1);
    pthread_mutex_lock(&s_wake_mu);
    pthread_cond_signal(&s_wake_cv);
    pthread_mutex_unlock(&s_wake_mu);
    (void)pthread_join(s_thread, NULL);
    fl_wq_shutdown(s_wq); /* protected items run once more */
    fl_wq_destroy(s_wq);  /* the thread is joined: nothing can touch the queue, so a restart initialises it afresh */
    pthread_cond_destroy(&s_wake_cv);
    s_started = 0;
    atomic_store(&s_heartbeat_ns, 0);
    fl_session_set_login_hooks(NULL, NULL, NULL);
    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_USER_DB_MAX_USERS; i++)
        if (s_slots[i].used)
            slot_wipe_locked(&s_slots[i]);
    memset(s_objects, 0, sizeof s_objects);
    pthread_mutex_unlock(&s_mu);
}


fl_result_t fl_kq_service_submit(const fl_wq_work_t *work) {
    fl_wq_work_t w;
    fl_result_t rc;

    if (!work || !s_started)
        return FL_RESULT_INVAL;
    w = *work;
    w.flags |= FL_WQ_PROTECTED;
    rc = fl_wq_enqueue(s_wq, &w);
    pthread_mutex_lock(&s_wake_mu);
    pthread_cond_signal(&s_wake_cv);
    pthread_mutex_unlock(&s_wake_mu);
    return rc; /* BUSY means the caller waits and retries: a signature is never discarded */
}

fl_result_t fl_kq_service_submit_every(const fl_wq_work_t *work, int64_t period_ns) {
    fl_wq_work_t w;

    if (!work || !s_started)
        return FL_RESULT_INVAL;
    w = *work;
    w.flags |= FL_WQ_PROTECTED;
    return fl_wq_enqueue_every(s_wq, &w, period_ns);
}


static void autosign_job(void *arg) {
    kq_object_t *o = (kq_object_t *)arg;
    char author[FL_HERITAGE_KEY_ID_MAX];
    uint8_t key[FL_USER_SIGNING_SECRET_LEN];
    kq_signing_mode_t mode = 0;
    int64_t now = 0, due = 0;
    fl_result_t rc;

    (void)fl_time_wall_sec(&now);
    if (__builtin_add_overflow(o->spec.last_signature_sec(o->spec.ctx), o->spec.interval_sec, &due) || now < due)
        return; /* a user signature moved the clock (an overflowing due time is never due) */
    author[0] = '\0';
    rc = o->spec.head_author(o->spec.ctx, author, sizeof author);
    if (rc == FL_RESULT_BUSY) { /* two heads: nobody is "the" author */
        fl_audit_identity_event(author, "kq-autosign-forked", o->label, NULL);
        return;
    }
    if (rc != FL_RESULT_OK)
        return;
    if (fl_kq_obtain_author_key(author, key, &mode) != FL_RESULT_OK) {
        fl_audit_identity_event(author, "kq-autosign-wait", o->label, NULL); /* tried again next period */
        return;
    }
    if (o->spec.sign(o->spec.ctx, author, key, mode) == FL_RESULT_OK)
        fl_audit_identity_event(author, "kq-autosign", o->label, mode == KQ_SIGNING_OWN ? "own" : "assembled");
    mem_domain_secure_zero(key, sizeof key); /* the copy is wiped at once */
}

fl_result_t fl_kq_service_autosign_add(const fl_kq_autosign_t *spec) {
    fl_wq_work_t w = {.fn = autosign_job, .pq_layer = FL_WQ_LAYER_BACKGROUND};
    kq_object_t *o = NULL;
    fl_result_t rc;

    if (!spec || !spec->label || !spec->last_signature_sec || !spec->head_author || !spec->sign ||
        spec->interval_sec <= 0 || spec->interval_sec > INT64_MAX / 1000000000ll || !s_started)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_KQ_AUTOSIGN_MAX && !o; i++)
        if (!s_objects[i].used)
            o = &s_objects[i];
    if (o) {
        o->used = 1;
        o->spec = *spec;
        snprintf(o->label, sizeof o->label, "%s", spec->label);
        o->spec.label = o->label;
    }
    pthread_mutex_unlock(&s_mu);
    if (!o)
        return FL_RESULT_NOMEM;
    w.ctx = o;
    rc = fl_kq_service_submit_every(&w, spec->interval_sec * 1000000000ll);
    if (rc != FL_RESULT_OK) {
        pthread_mutex_lock(&s_mu);
        o->used = 0;
        pthread_mutex_unlock(&s_mu);
    }
    return rc;
}

fl_result_t fl_kq_autosign_head_author(void *ctx, char *out, size_t cap) {
    const fl_kq_autosign_file_t *f = (const fl_kq_autosign_file_t *)ctx;
    fl_kq_cp_result_t cp;
    fl_result_t rc;

    if (!f || !f->path || !out || cap == 0)
        return FL_RESULT_INVAL;
    cp = fl_kq_cp_head_author(f->path, out, cap);
    if (cp == FL_KQ_CP_OK)
        return FL_RESULT_OK;
    if (cp == FL_KQ_CP_FORKED)
        return FL_RESULT_BUSY; /* the one result the sweep reads as "two heads" */
    rc = fl_kq_cp_to_result(cp);
    return rc == FL_RESULT_OK || rc == FL_RESULT_BUSY ? FL_RESULT_ERR : rc;
}

void fl_kq_service_utc_now(char *out, size_t cap) {
    int64_t sec = 0;
    time_t t;
    struct tm tm;

    if (!out || cap == 0)
        return;
    out[0] = '\0';
    if (fl_time_wall_sec(&sec) != FL_RESULT_OK)
        sec = (int64_t)time(NULL);
    t = (time_t)sec;
    if (gmtime_r(&t, &tm))
        (void)strftime(out, cap, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
}

/*
 * The one table from a refusal's result to KeyQuorum's closed KQER sets (keyquorum_error.h: codes
 * 1 Framing, 2 Untrusted, 8 Denied; classes 1 framing, 2 auth, 5 policy, 7 resource). Only a
 * refusal of the letter itself is reported; a local problem (no driver, I/O, a file already at the
 * destination) is kept to retry and has no row.
 */
static const struct {
    fl_result_t why;
    uint16_t code;
    uint8_t class_code;
} s_refusal_codes[] = {
    {FL_RESULT_INVAL, 1u, 1u},  /* malformed delivery: Framing, framing */
    {FL_RESULT_NOENT, 2u, 2u},  /* a party has no key: Untrusted, auth */
    {FL_RESULT_ACCES, 8u, 5u},  /* signature, trust, freshness or fork refusal: Denied, policy */
    {FL_RESULT_NOMEM, 8u, 7u},  /* too large: Denied, resource */
};

void fl_kq_report_refusal(const kq_file_offer_t *o, fl_result_t why) {
    const char *me = fl_session_current_key_id();
    uint8_t key[FL_USER_SIGNING_SECRET_LEN];
    uint8_t ref[32];
    char at[40];
    kq_signing_mode_t mode = 0;
    size_t i;

    if (!o)
        return;
    for (i = 0; i < sizeof s_refusal_codes / sizeof s_refusal_codes[0]; i++)
        if (s_refusal_codes[i].why == why)
            break;
    if (i == sizeof s_refusal_codes / sizeof s_refusal_codes[0])
        return; /* not a refusal of the letter */
    /* Signed only as the account that is online; nobody online signs nothing (fail closed). */
    if (!me || !me[0] || fl_kq_obtain_author_key(me, key, &mode) != FL_RESULT_OK) {
        fl_audit_identity_event(me && me[0] ? me : "UNKNOWN", "kq-report-offline", "receive", NULL);
        return;
    }
    fl_kq_service_utc_now(at, sizeof at);
    if (fl_kq_cp_offer_ref(o, ref) != FL_RESULT_OK ||
        fl_kq_cp_report_write(s_refusal_codes[i].code, s_refusal_codes[i].class_code,
                              (uint8_t)FL_KQ_OP_RECEIVE, ref, me, at, key,
                              g_cwd[0] ? g_cwd : ".") != FL_KQ_CP_OK)
        fl_audit_identity_event(me, "kq-report-failed", "receive", NULL);
    else
        fl_audit_identity_event(me, "kq-report", "receive", NULL);
    mem_domain_secure_zero(key, sizeof key);
}
