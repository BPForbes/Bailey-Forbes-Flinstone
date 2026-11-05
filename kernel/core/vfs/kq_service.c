#include "kq_service.h"

#include "bg_jobs.h"
#include "fl/audit_log.h"
#include "fl/authz_subsystem.h"
#include "fl/driver/keyquorum.h"
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

/* ---- key table ---------------------------------------------------------------------------- */

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

fl_result_t fl_kq_service_online_from_login(const char *account, const char *password) {
    uint8_t secret[FL_USER_SIGNING_SECRET_LEN];
    const fl_user_db_t *db = fl_session_user_db();
    const fl_user_record_t *u;
    fl_result_t rc;

    if (!account || !password)
        return FL_RESULT_INVAL;
    u = fl_user_db_find(db, account);
    /* The key derivation is slow: the session lock is not held here. open_key wipes secret on failure. */
    rc = u ? fl_user_db_open_key(db, account, password, secret) : FL_RESULT_NOENT;
    if (rc == FL_RESULT_OK)
        rc = slot_put(u->key_id, secret);
    mem_domain_secure_zero(secret, sizeof secret);
    fl_audit_identity_event(u ? u->key_id : account, rc == FL_RESULT_OK ? "kq-service-online" : "kq-service-refused",
                            u ? u->key_id : account, NULL);
    return rc;
}

void fl_kq_service_offline(const char *account) {
    const fl_user_record_t *u;
    int i;

    if (!account)
        return;
    u = fl_user_db_find(fl_session_user_db(), account);
    pthread_mutex_lock(&s_mu);
    i = slot_find_locked(u ? u->key_id : account);
    if (i >= 0)
        slot_wipe_locked(&s_slots[i]);
    pthread_mutex_unlock(&s_mu);
    if (i >= 0)
        fl_audit_identity_event(u ? u->key_id : account, "kq-service-offline", u ? u->key_id : account, NULL);
}

size_t fl_kq_service_online_count(void) {
    size_t n = 0;

    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_USER_DB_MAX_USERS; i++)
        n += s_slots[i].used ? 1u : 0u;
    pthread_mutex_unlock(&s_mu);
    return n;
}

/* Where Root's credential would be stored for unattended boot is an open decision (the image region of
 * FL-20 needs a written exception and a threat model); nothing is stored, so there is no fallback. */
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
    /* A parent author is assembled from its children's shares once KQ-18 exists. A leaf who is not signed
     * in cannot be assembled by anything: the answer is ACCES. */
    return rc;
}

/* ---- the service thread ------------------------------------------------------------------- */

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
        return FL_RESULT_ERR;
    }
    s_started = 1;
    fl_session_set_login_hooks(fl_kq_service_online_from_login, fl_kq_service_offline);
    fl_authz_subsystem_set_deny_audit(audit_control_denied);
    (void)fl_bg_watchdog_register(fl_kq_service_alive, fl_kq_service_report_stalled);
    return FL_RESULT_OK;
}

void fl_kq_service_shutdown(void) {
    if (!s_started)
        return;
    atomic_store(&s_stop, 1);
    pthread_mutex_lock(&s_wake_mu);
    pthread_cond_signal(&s_wake_cv);
    pthread_mutex_unlock(&s_wake_mu);
    (void)pthread_join(s_thread, NULL);
    fl_wq_shutdown(s_wq); /* protected items run once more */
    pthread_cond_destroy(&s_wake_cv);
    s_started = 0;
    atomic_store(&s_heartbeat_ns, 0);
    fl_session_set_login_hooks(NULL, NULL);
    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_USER_DB_MAX_USERS; i++)
        if (s_slots[i].used)
            slot_wipe_locked(&s_slots[i]);
    memset(s_objects, 0, sizeof s_objects);
    pthread_mutex_unlock(&s_mu);
}

/* ---- protected work ----------------------------------------------------------------------- */

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

/* ---- the periodic sweep ------------------------------------------------------------------- */

static void autosign_job(void *arg) {
    kq_object_t *o = (kq_object_t *)arg;
    char author[FL_HERITAGE_KEY_ID_MAX];
    uint8_t key[FL_USER_SIGNING_SECRET_LEN];
    kq_signing_mode_t mode = 0;
    int64_t now = 0;
    fl_result_t rc;

    (void)fl_time_wall_sec(&now);
    if (now < o->spec.last_signature_sec(o->spec.ctx) + o->spec.interval_sec)
        return; /* a user signature moved the clock */
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
        spec->interval_sec <= 0 || !s_started)
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
