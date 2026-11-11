#include "kq_service.h"

#include "bg_jobs.h"
#include "common.h"
#include "fl/audit_log.h"
#include "fl/authz_subsystem.h"
#include "fl/driver/keyquorum.h"
#include "fl/kq.h"
#include "heritage.h"
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
    uint8_t box[32]; /* FL-15: the box secret of a KQST login; has_box says whether it was published */
    int has_box;
    int session; /* published by a session login (a generation was given): the replacement rule below applies */
    int used;
} kq_slot_t;

typedef struct {
    int used;   /* the slot is taken: its periodic item is queued and can never be cancelled */
    int active; /* registered now; cleared by fl_kq_service_autosign_remove, the item then does nothing */
    fl_kq_autosign_t spec;
    char label[96];
    fl_kq_autosign_file_t file; /* ctx of a tracked file (fl_kq_service_autosign_track_file) */
    char path[512];
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
    mem_domain_secure_zero(s->box, sizeof s->box);
    s->has_box = 0;
    s->session = 0;
    mem_domain_zero(s->key_id, sizeof s->key_id);
    s->used = 0;
}

/* session non-zero: the key comes from a session login. The session has one account, so a login that
 * replaces another without a logout in between ends the previous account's slot here, secret and box key
 * together. The caller holds the session lock and has checked the login generation (publish_if_current), so
 * a stale login never reaches this and cannot wipe a later login's slot. A key brought online without a
 * session (the Root fallback, generation 0) is not session-owned and is left alone. */
static fl_result_t slot_put(const char *key_id, const uint8_t secret[FL_USER_SIGNING_SECRET_LEN], int session) {
    fl_result_t rc = FL_RESULT_NOMEM;
    int i;

    pthread_mutex_lock(&s_mu);
    if (session)
        for (size_t k = 0; k < FL_USER_DB_MAX_USERS; k++)
            if (s_slots[k].used && s_slots[k].session && strcmp(s_slots[k].key_id, key_id) != 0)
                slot_wipe_locked(&s_slots[k]);
    i = slot_find_locked(key_id);
    if (i < 0)
        for (size_t k = 0; k < FL_USER_DB_MAX_USERS && i < 0; k++)
            if (!s_slots[k].used)
                i = (int)k;
    if (i >= 0) {
        slot_wipe_locked(&s_slots[i]); /* a reused slot is wiped first */
        snprintf(s_slots[i].key_id, sizeof s_slots[i].key_id, "%s", key_id);
        mem_domain_copy(s_slots[i].secret, secret, sizeof s_slots[i].secret);
        s_slots[i].used = 1;
        s_slots[i].session = session;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_mu);
    return rc;
}

typedef struct {
    const char *key_id;
    const uint8_t *secret;
    int session;
} slot_put_ctx_t;

static fl_result_t slot_put_cb(void *p) {
    const slot_put_ctx_t *c = (const slot_put_ctx_t *)p;

    return slot_put(c->key_id, c->secret, c->session);
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
        slot_put_ctx_t put = {account->key_id, secret, login_generation != 0};

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
    put.session = login_generation != 0;
    rc = fl_session_publish_if_current(login_generation, slot_put_cb, &put);
    fl_audit_identity_event(account->key_id,
                            rc == FL_RESULT_OK ? "kq-service-online"
                            : rc == FL_RESULT_BUSY ? "kq-service-stale-login"
                                                   : "kq-service-refused",
                            account->key_id, NULL);
    return rc;
}

typedef struct {
    const char *key_id;
    const uint8_t *box;
} slot_box_ctx_t;

static fl_result_t slot_box_cb(void *p) {
    const slot_box_ctx_t *c = (const slot_box_ctx_t *)p;
    fl_result_t rc = FL_RESULT_ACCES;
    int i;

    pthread_mutex_lock(&s_mu);
    i = slot_find_locked(c->key_id);
    if (i >= 0) { /* only an account that is online has a slot to keep it in */
        mem_domain_copy(s_slots[i].box, c->box, sizeof s_slots[i].box);
        s_slots[i].has_box = 1;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_mu);
    return rc;
}

fl_result_t fl_kq_service_online_box(const fl_user_record_t *account, const uint8_t box_secret[32],
                                     uint64_t login_generation) {
    slot_box_ctx_t put;
    fl_result_t rc;

    if (!account || !box_secret)
        return FL_RESULT_INVAL;
    put.key_id = account->key_id;
    put.box = box_secret;
    rc = fl_session_publish_if_current(login_generation, slot_box_cb, &put);
    fl_audit_identity_event(account->key_id,
                            rc == FL_RESULT_OK ? "kq-service-box-online"
                            : rc == FL_RESULT_BUSY ? "kq-service-stale-login"
                                                   : "kq-service-refused",
                            account->key_id, NULL);
    return rc;
}

fl_result_t fl_kq_obtain_box_key(const char *author_key_id, uint8_t out[32]) {
    fl_result_t rc = FL_RESULT_ACCES;
    int i;

    if (!author_key_id || !out)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_mu);
    i = slot_find_locked(author_key_id);
    if (i >= 0 && s_slots[i].has_box) {
        mem_domain_copy(out, s_slots[i].box, sizeof s_slots[i].box);
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_mu);
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

/* Where Root's credential is stored for unattended boot is an open decision (the image region needs a written
 * exception and a threat model), so the service asks the runtime's provider and has none of its own: with no
 * provider there is no fallback. */
static fl_kq_root_cred_fn s_root_cred;

void fl_kq_service_set_root_credential_provider(fl_kq_root_cred_fn provider) {
    pthread_mutex_lock(&s_mu);
    s_root_cred = provider;
    pthread_mutex_unlock(&s_mu);
}

static fl_result_t stored_root_credential(char *out, size_t cap) {
    fl_kq_root_cred_fn provider;

    pthread_mutex_lock(&s_mu);
    provider = s_root_cred;
    pthread_mutex_unlock(&s_mu);
    if (!provider || !out || cap == 0)
        return FL_RESULT_NOENT;
    out[0] = '\0';
    return provider(out, cap);
}

fl_result_t fl_kq_service_root_fallback(void) {
    char cred[128];
    fl_result_t rc = stored_root_credential(cred, sizeof cred);

    if (rc != FL_RESULT_OK) {
        mem_domain_secure_zero(cred, sizeof cred); /* a provider that failed part-way still wrote into it */
        return rc == FL_RESULT_NOENT ? FL_RESULT_NOENT : FL_RESULT_ACCES;
    }
    rc = fl_kq_service_online_from_login(FL_HERITAGE_ROOT_KEY, cred);
    mem_domain_secure_zero(cred, sizeof cred);
    fl_audit_identity_event(FL_HERITAGE_ROOT_KEY, rc == FL_RESULT_OK ? "kq-root-fallback" : "kq-root-fallback-refused",
                            "service", NULL); /* refused once, no retry here: the caller decides when to ask again */
    return rc;
}

/* A parent who is not signed in, put together from the children who are (KQ-16). The record is the KQAS
 * bytes the account store holds for the parent; each online child with a box secret (FL-15) and a device
 * id in the account registry (the pairing row of its KQST) contributes its label, box secret and device
 * id, and KeyQuorum decides whether that is a quorum and whether the recovered seed is the parent's
 * registered key. Nothing is decided here beyond "who is online". Every secret copy is wiped. */
#define KQ_ASSEMBLY_CHILDREN_MAX FL_USER_DB_MAX_USERS

static fl_result_t assemble_author_key(const char *parent, uint8_t out[32]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    const fl_user_db_t *db = fl_session_user_db();
    const unsigned char *parent_pub;
    char labels[KQ_ASSEMBLY_CHILDREN_MAX][FL_HERITAGE_KEY_ID_MAX];
    const char *label_ptr[KQ_ASSEMBLY_CHILDREN_MAX];
    uint8_t box[KQ_ASSEMBLY_CHILDREN_MAX][32];
    uint8_t dev[KQ_ASSEMBLY_CHILDREN_MAX][16];
    char up[FL_HERITAGE_KEY_ID_MAX];
    unsigned char *record;
    unsigned generation = 0, threshold = 0;
    size_t rlen = 0, n = 0;
    fl_result_t rc = FL_RESULT_ACCES;
    int st;

    if (!drv || !drv->assembly_assemble || !db || !(parent_pub = fl_user_db_signing_public(db, parent)))
        return FL_RESULT_ACCES; /* no driver op (the freestanding image), or no registered key to match */
    record = (unsigned char *)mem_domain_alloc(MEM_DOMAIN_FS, FL_USER_ASSEMBLY_MAX);
    if (!record)
        return FL_RESULT_NOMEM;
    if (fl_user_db_assembly_get(db, parent, record, FL_USER_ASSEMBLY_MAX, &rlen, &generation, &threshold) !=
        FL_RESULT_OK)
        goto done; /* this account was never split: nothing to wait for either */
    /* The children online now: direct heritage children of the parent that published a box secret. */
    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_USER_DB_MAX_USERS && n < KQ_ASSEMBLY_CHILDREN_MAX; i++) {
        if (!s_slots[i].used || !s_slots[i].has_box || fl_heritage_parent(s_slots[i].key_id, up, sizeof up) != FL_RESULT_OK ||
            strcmp(up, parent) != 0)
            continue;
        mem_domain_copy(labels[n], s_slots[i].key_id, sizeof labels[n]);
        mem_domain_copy(box[n], s_slots[i].box, sizeof box[n]);
        n++;
    }
    pthread_mutex_unlock(&s_mu);
    /* The device id of each child comes from the account registry; a child with no pairing row cannot be counted. */
    size_t k = 0;

    for (size_t i = 0; i < n; i++) {
        unsigned char sp[32], ep[32];

        if (fl_user_db_kqst_pairing(db, labels[i], dev[k], sp, ep) != FL_RESULT_OK)
            continue;
        if (k != i) {
            mem_domain_copy(labels[k], labels[i], sizeof labels[k]);
            mem_domain_copy(box[k], box[i], sizeof box[k]);
        }
        k++;
    }
    n = k;
    for (size_t i = 0; i < n; i++)
        label_ptr[i] = labels[i];
    if (n == 0)
        goto done; /* nobody to assemble from: the sweep waits and asks again */
    /* One physical device per share (the record's threshold distinct devices): slots of one container are no quorum. */
    st = drv->assembly_assemble(record, rlen, parent_pub, label_ptr, (const uint8_t(*)[32])box,
                                (const uint8_t(*)[16])dev, n, threshold, out);
    if (st == KQ_ASSEMBLY_OK) {
        rc = FL_RESULT_OK;
        fl_audit_identity_event(parent, "kq-assembled", parent, NULL); /* never the seed, a share or a label's secret */
    } else {
        mem_domain_secure_zero(out, 32);
        fl_audit_identity_event(parent, "kq-assemble-refused", parent, NULL);
    }
done:
    mem_domain_secure_zero(box, sizeof box);
    mem_domain_secure_zero(record, FL_USER_ASSEMBLY_MAX);
    mem_domain_free(MEM_DOMAIN_FS, record);
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
        mem_domain_copy(out, s_slots[i].secret, FL_USER_SIGNING_SECRET_LEN);
        *mode = KQ_SIGNING_OWN;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_mu);
    if (i < 0 && assemble_author_key(author_key_id, out) == FL_RESULT_OK) {
        *mode = KQ_SIGNING_ASSEMBLED;
        rc = FL_RESULT_OK;
    }
    /* Neither signed in nor assemblable: ACCES, and the sweep waits. */
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
    fl_session_set_login_box_hook(fl_kq_service_online_box);
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
    fl_session_set_login_box_hook(NULL);
    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_USER_DB_MAX_USERS; i++)
        if (s_slots[i].used)
            slot_wipe_locked(&s_slots[i]);
    mem_domain_zero(s_objects, sizeof s_objects);
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
    fl_kq_autosign_t spec;
    char label[sizeof o->label];
    char author[FL_HERITAGE_KEY_ID_MAX];
    uint8_t key[FL_USER_SIGNING_SECRET_LEN];
    kq_signing_mode_t mode = 0;
    int64_t now = 0, due = 0;
    fl_result_t rc;

    /* A removed object's periodic item cannot be cancelled (the queue has no such call): it stays queued
     * and does nothing. The run below works on a copy, so a removal during it cannot change what it uses. */
    pthread_mutex_lock(&s_mu);
    if (!o->active) {
        pthread_mutex_unlock(&s_mu);
        return;
    }
    spec = o->spec;
    mem_domain_copy(label, o->label, sizeof label);
    spec.label = label;
    pthread_mutex_unlock(&s_mu);

    (void)fl_time_wall_sec(&now);
    if (__builtin_add_overflow(spec.last_signature_sec(spec.ctx), spec.interval_sec, &due) || now < due)
        return; /* a user signature moved the clock (an overflowing due time is never due) */
    author[0] = '\0';
    rc = spec.head_author(spec.ctx, author, sizeof author);
    if (rc == FL_RESULT_BUSY) { /* two heads: nobody is "the" author */
        fl_audit_identity_event(author, "kq-autosign-forked", label, NULL);
        return;
    }
    if (rc != FL_RESULT_OK)
        return;
    if (fl_kq_obtain_author_key(author, key, &mode) != FL_RESULT_OK) {
        fl_audit_identity_event(author, "kq-autosign-wait", label, NULL); /* tried again next period */
        return;
    }
    if (spec.sign(spec.ctx, author, key, mode) == FL_RESULT_OK)
        fl_audit_identity_event(author, "kq-autosign", label, mode == KQ_SIGNING_OWN ? "own" : "assembled");
    mem_domain_secure_zero(key, sizeof key); /* the copy is wiped at once */
}

/* Takes a slot, fills it completely (a tracked file's path and ctx included), then queues its periodic
 * item: nothing runs on a half-built object. */
static fl_result_t autosign_register(const fl_kq_autosign_t *spec, const char *path) {
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
        mem_domain_zero(o, sizeof *o);
        o->used = 1;
        o->active = 1;
        o->spec = *spec;
        snprintf(o->label, sizeof o->label, "%s", spec->label);
        o->spec.label = o->label;
        if (path) {
            snprintf(o->path, sizeof o->path, "%s", path);
            o->file.path = o->path;
            o->spec.ctx = &o->file;
        }
    }
    pthread_mutex_unlock(&s_mu);
    if (!o)
        return FL_RESULT_NOMEM;
    w.ctx = o;
    rc = fl_kq_service_submit_every(&w, spec->interval_sec * 1000000000ll);
    if (rc != FL_RESULT_OK) {
        pthread_mutex_lock(&s_mu);
        o->used = 0;
        o->active = 0;
        pthread_mutex_unlock(&s_mu);
    }
    return rc;
}

fl_result_t fl_kq_service_autosign_add(const fl_kq_autosign_t *spec) {
    return autosign_register(spec, NULL);
}

fl_result_t fl_kq_service_autosign_remove(const char *label) {
    fl_result_t rc = FL_RESULT_NOENT;

    if (!label || !s_started)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_KQ_AUTOSIGN_MAX; i++)
        /* A tracked file is known by its full path (the label is cut to fit, so two long paths can share one);
         * any other registration is known by its label. */
        if (s_objects[i].used && s_objects[i].active &&
            strcmp(s_objects[i].path[0] ? s_objects[i].path : s_objects[i].label, label) == 0) {
            s_objects[i].active = 0; /* the slot stays taken: its queued item will find it inactive */
            rc = FL_RESULT_OK;
        }
    pthread_mutex_unlock(&s_mu);
    return rc;
}

int64_t fl_kq_autosign_last_signature(void *ctx) {
    const fl_kq_autosign_file_t *f = (const fl_kq_autosign_file_t *)ctx;
    int64_t sec = 0;

    if (!f || !f->path || fl_kq_cp_last_signature(f->path, &sec) != FL_KQ_CP_OK)
        return 0; /* nothing readable is also nothing to sign: head_author fails the same way */
    return sec;
}

fl_result_t fl_kq_autosign_sign(void *ctx, const char *author, const uint8_t key[32], kq_signing_mode_t mode) {
    const fl_kq_autosign_file_t *f = (const fl_kq_autosign_file_t *)ctx;
    char at[40];
    fl_kq_cp_result_t cp;
    fl_result_t rc;

    (void)mode; /* own or assembled is audited by the sweep */
    if (!f || !f->path || !author || !key)
        return FL_RESULT_INVAL;
    fl_kq_service_utc_now(at, sizeof at);
    cp = fl_kq_cp_sign_head_with(f->path, author, key, at);
    if (cp == FL_KQ_CP_OK)
        return FL_RESULT_OK;
    if (cp == FL_KQ_CP_FORKED)
        return FL_RESULT_BUSY;
    rc = fl_kq_cp_to_result(cp);
    return rc == FL_RESULT_OK || rc == FL_RESULT_BUSY ? FL_RESULT_ERR : rc;
}

fl_result_t fl_kq_service_autosign_track_file(const char *path, int64_t interval_sec) {
    fl_kq_autosign_t spec = {.label = path,
                             .last_signature_sec = fl_kq_autosign_last_signature,
                             .head_author = fl_kq_autosign_head_author,
                             .sign = fl_kq_autosign_sign,
                             .interval_sec = interval_sec};
    int revived = 0;

    if (!path || !path[0] || strlen(path) >= sizeof s_objects[0].path || !s_started)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_mu);
    for (size_t i = 0; i < FL_KQ_AUTOSIGN_MAX && !revived; i++)
        if (s_objects[i].used && s_objects[i].path[0] && strcmp(s_objects[i].path, path) == 0) {
            s_objects[i].active = 1; /* tracked again: its queued item resumes, no second item */
            revived = 1;
        }
    pthread_mutex_unlock(&s_mu);
    return revived ? FL_RESULT_OK : autosign_register(&spec, path);
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

void fl_kq_report_refusal_in(const kq_file_offer_t *o, fl_result_t why, const fl_kq_dir_t *dir) {
    const char *me = fl_session_current_key_id();
    const char *who = me && me[0] ? me : "UNKNOWN";
    const char *event;
    uint8_t key[FL_USER_SIGNING_SECRET_LEN];
    uint8_t ref[32];
    char at[40];
    kq_signing_mode_t mode = 0;
    fl_kq_cp_result_t cp;
    size_t i;

    if (!o)
        return;
    for (i = 0; i < sizeof s_refusal_codes / sizeof s_refusal_codes[0]; i++)
        if (s_refusal_codes[i].why == why)
            break;
    if (i == sizeof s_refusal_codes / sizeof s_refusal_codes[0])
        return; /* not a refusal of the letter */
    /* No held landing directory: nothing is written, never a guessed path. */
    if (!dir) {
        fl_audit_identity_event(who, "kq-report-uncontained", "receive", NULL);
        return;
    }
    mem_domain_secure_zero(key, sizeof key);
    /* Signed only as the account that is online; nobody online signs nothing (fail closed). */
    if (!me || !me[0] || fl_kq_obtain_author_key(me, key, &mode) != FL_RESULT_OK) {
        event = "kq-report-offline";
    } else {
        fl_kq_service_utc_now(at, sizeof at);
        cp = fl_kq_cp_offer_ref(o, ref) != FL_RESULT_OK
                 ? FL_KQ_CP_INVALID
                 : fl_kq_cp_report_write_in(s_refusal_codes[i].code, s_refusal_codes[i].class_code,
                                            (uint8_t)FL_KQ_OP_RECEIVE, ref, me, at, key, dir);
        event = cp == FL_KQ_CP_OK          ? "kq-report"
                : cp == FL_KQ_CP_EXISTS    ? "kq-report-exists"
                : cp == FL_KQ_CP_TOO_LARGE ? "kq-report-capped"
                                           : "kq-report-failed";
    }
    mem_domain_secure_zero(key, sizeof key); /* on every path, a failed lookup included */
    fl_audit_identity_event(who, event, "receive", NULL);
}

void fl_kq_report_refusal(const kq_file_offer_t *o, fl_result_t why) {
    fl_kq_report_refusal_in(o, why, NULL);
}
