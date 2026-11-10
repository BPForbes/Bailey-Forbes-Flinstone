/*
 * The KeyQuorum key service (FL-20, absorbs FL-24): online through a login, offline at logout, no key for
 * `su`, a thread that cannot be cancelled or signalled, protected work that is never dropped, a heartbeat
 * the watchdog reads, the control op refused for everyone, and the periodic sweep.
 */
#include "kq_service.h"

#include "net_background.h"
#include "workqueue.h"

#include "bg_jobs.h"
#include "contract_p2_authz.h"
#include "fl/authz_subsystem.h"
#include "common.h"
#include "fl/audit_log.h"
#include "fl/hex.h"
#include "fl/kq.h"
#include "kq_checkpoint.h"
#include "mem_domain.h"
#include "server_shared_fs.h"
#include "session.h"
#include "timekeeping.h"
#include "user_db.h"

#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

/* bg_jobs.c ticks the network background; this test has no network core. */
void fl_net_background_init(void) {}
void fl_net_background_shutdown(void) {}
void fl_net_background_tick(unsigned max_items) { (void)max_items; }
fl_result_t fl_net_background_arp_tick_kick(void) { return FL_RESULT_OK; }

static void sleep_ms(unsigned ms) {
    struct timespec ts = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
}

static _Atomic int s_ran;
static void count_job(void *ctx) { (void)ctx; atomic_fetch_add(&s_ran, 1); }

static sem_t s_hold, s_holding;
static void blocking_job(void *ctx) {
    (void)ctx;
    sem_post(&s_holding);
    sem_wait(&s_hold);
}

static _Atomic int s_sigusr1;
static void on_usr1(int sig) { (void)sig; atomic_fetch_add(&s_sigusr1, 1); }

static const fl_user_record_t *account(const char *name) {
    return fl_user_db_find(fl_session_user_db(), name);
}

static int test_login_brings_the_key_online_and_logout_wipes_it(void) {
    uint8_t key[32], expect[32];
    kq_signing_mode_t mode = 0;
    const fl_user_record_t *flin = account("Flinstone");
    const fl_user_record_t *root = account("Root");

    ASSERT(flin && root && !fl_kq_service_alive());
    ASSERT(fl_kq_service_start() == FL_RESULT_OK);
    ASSERT(fl_kq_service_start() == FL_RESULT_OK); /* idempotent */
    ASSERT(fl_kq_service_alive());
    ASSERT(fl_kq_service_online_count() == 0);
    ASSERT(fl_kq_obtain_author_key(flin->key_id, key, &mode) == FL_RESULT_ACCES);

    ASSERT(fl_session_login("Flinstone", "wrong") == FL_RESULT_ACCES); /* a refused login brings nothing online */
    ASSERT(fl_kq_service_online_count() == 0);
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_OK);
    ASSERT(fl_kq_service_online_count() == 1);
    ASSERT(fl_user_db_open_key(fl_session_user_db(), "Flinstone", "flinstone", expect) == FL_RESULT_OK);
    ASSERT(fl_kq_obtain_author_key(flin->key_id, key, &mode) == FL_RESULT_OK);
    ASSERT(mode == KQ_SIGNING_OWN && memcmp(key, expect, 32) == 0);
    ASSERT(fl_kq_obtain_author_key(root->key_id, key, &mode) == FL_RESULT_ACCES); /* only the signed-in account */

    /* su without a password opens no key; restoring the original account keeps its key (su -c). */
    ASSERT(fl_session_set_user("Root") == FL_RESULT_OK);
    ASSERT(fl_kq_service_online_count() == 1);
    ASSERT(fl_kq_obtain_author_key(root->key_id, key, &mode) == FL_RESULT_ACCES);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    ASSERT(fl_kq_obtain_author_key(flin->key_id, key, &mode) == FL_RESULT_OK);
    ASSERT(mode == KQ_SIGNING_OWN && memcmp(key, expect, 32) == 0);

    ASSERT(fl_session_logout() == FL_RESULT_OK); /* the account that signed out loses its key */
    ASSERT(fl_kq_service_online_count() == 0);
    ASSERT(fl_kq_obtain_author_key(flin->key_id, key, &mode) == FL_RESULT_ACCES);
    ASSERT(fl_kq_service_root_fallback() == FL_RESULT_NOENT); /* no stored credential: the service stays up */
    ASSERT(fl_kq_service_alive());
    memset(expect, 0, sizeof expect);
    return 0;
}

static sem_t s_kqst_gate_entered, s_kqst_gate_release;
static char s_observer_saw[FL_USER_NAME_MAX];
static int s_observer_elev;

static void kqst_pre_open_gate(void) {
    sem_post(&s_kqst_gate_entered);
    sem_wait(&s_kqst_gate_release);
}

static void *kqst_login_observer(void *arg) {
    (void)arg;
    sem_wait(&s_kqst_gate_entered);
    snprintf(s_observer_saw, sizeof s_observer_saw, "%s", fl_session_current_user());
    s_observer_elev = fl_session_is_elevated_account();
    sem_post(&s_kqst_gate_release);
    return NULL;
}

/* KQST login must not expose the account (or its elevation) before the token opens. */
static int test_kqst_login_commits_only_after_token_open(void) {
    fl_user_db_t *db = fl_session_user_db_mut();
    pthread_t obs;
    const fl_user_record_t *root = account("Root");

    ASSERT(db && root);
    ASSERT(fl_session_login("Root", "root") == FL_RESULT_OK); /* known session before the KQST attempt */
    ASSERT(fl_user_db_set_auth_mode(db, "Flinstone", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(sem_init(&s_kqst_gate_entered, 0, 0) == 0);
    ASSERT(sem_init(&s_kqst_gate_release, 0, 0) == 0);
    s_observer_saw[0] = '\0';
    s_observer_elev = -1;
    fl_session_test_set_kqst_pre_open(kqst_pre_open_gate);
    ASSERT(pthread_create(&obs, NULL, kqst_login_observer, NULL) == 0);
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_ACCES);
    ASSERT(pthread_join(obs, NULL) == 0);
    fl_session_test_set_kqst_pre_open(NULL);
    sem_destroy(&s_kqst_gate_entered);
    sem_destroy(&s_kqst_gate_release);
    ASSERT(strcmp(s_observer_saw, "Root") == 0); /* still Root while the token was opening */
    ASSERT(s_observer_elev == 1);                 /* Root's elevation, not Flinstone's */
    ASSERT(strcmp(fl_session_current_user(), "Root") == 0);
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    ASSERT(fl_user_db_set_auth_mode(db, "Flinstone", FL_USER_AUTH_PASSWORD) == FL_RESULT_OK);
    return 0;
}

/* An account marked password-plus-KQST cannot sign in by password alone: no login, nothing online. */
static int test_a_kqst_account_cannot_log_in_by_password_alone(void) {
    fl_user_db_t *db = fl_session_user_db_mut();

    ASSERT(db && fl_user_db_set_auth_mode(db, "Flinstone", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_ACCES);
    ASSERT(fl_kq_service_online_count() == 0);
    ASSERT(fl_session_login("Flinstone", "wrong") == FL_RESULT_ACCES);
    ASSERT(fl_user_db_set_auth_mode(db, "Flinstone", FL_USER_AUTH_PASSWORD) == FL_RESULT_OK);
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_OK);
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    return 0;
}

/* FL-15: open refuses when the store has no pairing/path, or the token file is junk. There is no C
 * seal export: the success-path open is tests/test_kq_real_token.c (make test_kq_real_token), whose
 * KQST is minted by the keyquorum CLI. */
static int test_kqst_open_refusal_matrix(void) {
    fl_user_db_t *db = fl_session_user_db_mut();
    const fl_user_record_t *flin;
    char path_buf[256];
    char tokpath[] = "/tmp/fl_kqstXXXXXX";
    unsigned char device_id[16], signing_pub[32], encryption_pub[32];
    fl_kq_requirement_t req;
    sqlite3 *sql = NULL;
    int fd;
    const char *db_path;

    ASSERT(db);
    flin = account("Flinstone");
    ASSERT(flin);
    ASSERT(fl_user_db_set_auth_mode(db, "Flinstone", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);

    req = fl_kq_unlock_requirement("Flinstone", "Root", 0);
    ASSERT(req.factor == FL_KQ_ACCESS_KQST_PASSWORD); /* KeyQuorum: the target is Root */

    ASSERT(fl_user_db_kqst_path(db, flin->key_id, path_buf, sizeof path_buf) == FL_RESULT_NOENT);
    ASSERT(fl_user_db_kqst_pairing(db, flin->key_id, device_id, signing_pub, encryption_pub) ==
           FL_RESULT_NOENT);
    ASSERT(fl_kq_open_account("Flinstone", "flinstone", NULL) == FL_KQ_ACCOUNT_INVALID);
    ASSERT(fl_kq_open_account("Flinstone", "flinstone", "/no/such/token.kqst") == FL_KQ_ACCOUNT_INVALID);
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_ACCES);
    ASSERT(fl_kq_service_online_count() == 0);

    fd = mkstemp(tokpath);
    ASSERT(fd >= 0);
    ASSERT(write(fd, "not-a-kqst", 10) == 10);
    close(fd);
    /* Path alone is not enough: pairing must come from the caller's registry. */
    ASSERT(fl_kq_open_account("Flinstone", "flinstone", tokpath) == FL_KQ_ACCOUNT_INVALID);

    db_path = getenv("FL_USERS_DB_PATH");
    ASSERT(db_path && sqlite3_open(db_path, &sql) == SQLITE_OK);
    {
        sqlite3_stmt *st = NULL;

        ASSERT(sqlite3_prepare_v2(sql,
                                  "INSERT INTO account_keys (label,purpose,holds_share,minted_by,minted_at,"
                                  "generation,kqst_path,device_id,signing_public,encryption_public,"
                                  "ledger_seq,event_hash) VALUES (?1,'own',1,'Root',1,1,?2,"
                                  "x'01010101010101010101010101010101',"
                                  "x'0202020202020202020202020202020202020202020202020202020202020202',"
                                  "x'0303030303030303030303030303030303030303030303030303030303030303',"
                                  "0,x'00');",
                                  -1, &st, NULL) == SQLITE_OK);
        sqlite3_bind_text(st, 1, flin->key_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, tokpath, -1, SQLITE_TRANSIENT);
        ASSERT(sqlite3_step(st) == SQLITE_DONE);
        sqlite3_finalize(st);
    }
    sqlite3_close(sql);

    ASSERT(fl_user_db_kqst_pairing(db, flin->key_id, device_id, signing_pub, encryption_pub) ==
           FL_RESULT_OK);
    ASSERT(device_id[0] == 0x01 && signing_pub[0] == 0x02 && encryption_pub[0] == 0x03);
    ASSERT(fl_kq_open_account("Flinstone", "flinstone", NULL) == FL_KQ_ACCOUNT_INVALID); /* junk token */
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_ACCES);
    ASSERT(fl_kq_service_online_count() == 0);

    unlink(tokpath);
    ASSERT(fl_user_db_set_auth_mode(db, "Flinstone", FL_USER_AUTH_PASSWORD) == FL_RESULT_OK);
    return 0;
}

/* The key derivation runs outside the session lock, so a logout can overlap it. The key of a login that
 * has already ended must not come online afterwards; the publish is checked against the login generation
 * under the session lock. */
static int test_a_logout_that_overlaps_the_key_derivation_wins(void) {
    fl_user_record_t rec;
    uint64_t ended, again;
    const fl_user_record_t *flin = account("Flinstone");
    uint8_t key[32];
    kq_signing_mode_t mode = 0;

    ASSERT(flin && fl_session_snapshot_account("Flinstone", &rec) == FL_RESULT_OK);
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_OK);
    ASSERT(fl_kq_service_online_count() == 1);
    ended = fl_session_login_generation();
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    ASSERT(fl_kq_service_online_count() == 0);
    ASSERT(fl_session_login_generation() != ended); /* a logout starts a new generation */

    /* the derivation for the login that ended finishes late: refused, nothing published */
    ASSERT(fl_kq_service_online_for_session(&rec, "flinstone", ended) == FL_RESULT_BUSY);
    ASSERT(fl_kq_service_online_count() == 0);
    ASSERT(fl_kq_obtain_author_key(flin->key_id, key, &mode) == FL_RESULT_ACCES);

    /* a wrong password is refused whatever the generation, and a login that is still current publishes */
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_OK);
    again = fl_session_login_generation();
    ASSERT(again != ended);
    ASSERT(fl_kq_service_online_for_session(&rec, "wrong", again) == FL_RESULT_ACCES);
    ASSERT(fl_kq_service_online_for_session(&rec, "flinstone", again) == FL_RESULT_OK);
    ASSERT(fl_kq_service_online_count() == 1);
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    ASSERT(fl_kq_service_online_count() == 0);
    memset(key, 0, sizeof key);
    return 0;
}

static int test_the_thread_cannot_be_cancelled_or_signalled(void) {
    pthread_t t = fl_kq_service_thread_for_test();
    fl_wq_work_t w = {.fn = count_job, .pq_layer = FL_WQ_LAYER_URGENT};
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_usr1;
    sigaction(SIGUSR1, &sa, NULL);
    atomic_store(&s_sigusr1, 0);
    atomic_store(&s_ran, 0);

    (void)pthread_cancel(t); /* what Ctrl+C does to a pool worker */
    sleep_ms(400);
    ASSERT(fl_kq_service_alive());
    ASSERT(fl_kq_service_submit(&w) == FL_RESULT_OK);
    sleep_ms(300);
    ASSERT(atomic_load(&s_ran) == 1); /* it is still running work */

    (void)pthread_kill(t, SIGUSR1); /* every signal is blocked on that thread */
    sleep_ms(300);
    ASSERT(atomic_load(&s_sigusr1) == 0);
    ASSERT(fl_kq_service_alive());
    return 0;
}

static int test_protected_work_is_never_dropped_and_a_stall_fails_closed(void) {
    fl_wq_work_t hold = {.fn = blocking_job, .pq_layer = FL_WQ_LAYER_URGENT};
    fl_wq_work_t w = {.fn = count_job, .pq_layer = FL_WQ_LAYER_NORMAL};
    unsigned accepted = 0;
    fl_result_t rc;

    sem_init(&s_hold, 0, 0);
    sem_init(&s_holding, 0, 0);
    atomic_store(&s_ran, 0);
    ASSERT(fl_kq_service_submit(&hold) == FL_RESULT_OK);
    sem_wait(&s_holding); /* the service thread is now busy in the job */
    for (;;) {
        rc = fl_kq_service_submit(&w);
        if (rc != FL_RESULT_OK)
            break;
        accepted++;
        ASSERT(accepted <= FL_WQ_PENDING_MAX);
    }
    ASSERT(rc == FL_RESULT_BUSY && accepted > 0); /* full means "retry", never a silent discard */

    sleep_ms(2400); /* the heartbeat goes stale while the thread is stuck: fail closed */
    ASSERT(!fl_kq_service_alive());
    fl_bg_watchdog_check(); /* the watchdog's check reports the stall (audited once) */
    fl_bg_watchdog_check();

    sem_post(&s_hold);
    sleep_ms(500);
    ASSERT(fl_kq_service_alive());
    ASSERT((unsigned)atomic_load(&s_ran) == accepted); /* every accepted job ran */
    return 0;
}

static fl_authz_decision_t allow_everything(unsigned op, void *ctx) { (void)op; (void)ctx; return FL_AUTHZ_ALLOW; }

static int test_nobody_may_control_the_service(void) {
    ASSERT(fl_authz_subsystem_check(FL_AUTHZ_OP_KQ_SERVICE_CONTROL, NULL) == FL_AUTHZ_DENY);
    fl_authz_subsystem_set_hook(allow_everything, NULL); /* an installed hook cannot allow it */
    ASSERT(fl_authz_subsystem_check(FL_AUTHZ_OP_KQ_SERVICE_CONTROL, NULL) == FL_AUTHZ_DENY);
    ASSERT(fl_authz_subsystem_check(FL_AUTHZ_OP_MOUNT, NULL) == FL_AUTHZ_ALLOW); /* other ops unchanged */
    fl_authz_subsystem_set_hook(NULL, NULL);
    setenv("FL_PRINCIPAL", "guest", 1);
    ASSERT(fl_authz_subsystem_check(FL_AUTHZ_OP_KQ_SERVICE_CONTROL, NULL) == FL_AUTHZ_DENY);
    unsetenv("FL_PRINCIPAL");
    return 0;
}

typedef struct {
    int64_t last_sig;
    fl_result_t head_rc;
    char author[FL_HERITAGE_KEY_ID_MAX];
    _Atomic int signed_count;
    uint8_t seen_key[32];
    kq_signing_mode_t seen_mode;
} fake_obj_t;

static int64_t fake_last(void *c) { return ((fake_obj_t *)c)->last_sig; }
static fl_result_t fake_head(void *c, char *out, size_t cap) {
    fake_obj_t *o = (fake_obj_t *)c;
    snprintf(out, cap, "%s", o->author);
    return o->head_rc;
}
static fl_result_t fake_sign(void *c, const char *author, const uint8_t key[32], kq_signing_mode_t m) {
    fake_obj_t *o = (fake_obj_t *)c;
    (void)author;
    memcpy(o->seen_key, key, 32);
    o->seen_mode = m;
    atomic_fetch_add(&o->signed_count, 1);
    return FL_RESULT_OK;
}

static int test_the_periodic_sweep(void) {
    static fake_obj_t recent, due, forked, away;
    const fl_user_record_t *flin = account("Flinstone"), *root = account("Root");
    fl_kq_autosign_t spec = {.last_signature_sec = fake_last, .head_author = fake_head, .sign = fake_sign,
                             .interval_sec = 1};
    uint8_t expect[32];
    int64_t now = 0;

    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(fl_session_user_db(), "Flinstone", "flinstone", expect) == FL_RESULT_OK);
    (void)fl_time_wall_sec(&now);
    /* an interval whose nanoseconds would overflow is refused, not scheduled as a wrapped period */
    spec.label = "overflow"; spec.ctx = &recent; spec.interval_sec = INT64_MAX;
    ASSERT(fl_kq_service_autosign_add(&spec) == FL_RESULT_INVAL);
    spec.interval_sec = 1;

    recent.last_sig = now + 3600; recent.head_rc = FL_RESULT_OK; snprintf(recent.author, sizeof recent.author, "%s", flin->key_id);
    due.last_sig = 0;             due.head_rc = FL_RESULT_OK;    snprintf(due.author, sizeof due.author, "%s", flin->key_id);
    forked.last_sig = 0;          forked.head_rc = FL_RESULT_BUSY; snprintf(forked.author, sizeof forked.author, "%s", flin->key_id);
    away.last_sig = 0;            away.head_rc = FL_RESULT_OK;   snprintf(away.author, sizeof away.author, "%s", root->key_id); /* not signed in */

    spec.label = "recent"; spec.ctx = &recent; ASSERT(fl_kq_service_autosign_add(&spec) == FL_RESULT_OK);
    spec.label = "due";    spec.ctx = &due;    ASSERT(fl_kq_service_autosign_add(&spec) == FL_RESULT_OK);
    spec.label = "forked"; spec.ctx = &forked; ASSERT(fl_kq_service_autosign_add(&spec) == FL_RESULT_OK);
    spec.label = "away";   spec.ctx = &away;   ASSERT(fl_kq_service_autosign_add(&spec) == FL_RESULT_OK);

    sleep_ms(2600);
    ASSERT(atomic_load(&due.signed_count) >= 1);                  /* signed by the author's own key */
    ASSERT(due.seen_mode == KQ_SIGNING_OWN && memcmp(due.seen_key, expect, 32) == 0);
    ASSERT(atomic_load(&recent.signed_count) == 0);               /* a recent signature moves the clock */
    ASSERT(atomic_load(&forked.signed_count) == 0);               /* two heads: nobody is the author */
    ASSERT(atomic_load(&away.signed_count) == 0);                 /* the author is not signed in: it waits */

    /* once the author signs in, the waiting object is signed on a later period */
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    ASSERT(fl_session_login("Root", "root") == FL_RESULT_OK);
    sleep_ms(2200);
    ASSERT(atomic_load(&away.signed_count) >= 1);
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    return 0;
}

/* The audit lines the service writes, captured through the audit sink (the service thread emits). */
static pthread_mutex_t s_audit_mu = PTHREAD_MUTEX_INITIALIZER;
static char s_audit[8192];
static void audit_emit(struct fl_log_sink *sink, int level, int facility, const char *msg) {
    (void)sink; (void)level; (void)facility;
    pthread_mutex_lock(&s_audit_mu);
    if (msg && strlen(s_audit) + strlen(msg) + 2 < sizeof s_audit) {
        strcat(s_audit, msg);
        strcat(s_audit, "\n");
    }
    pthread_mutex_unlock(&s_audit_mu);
}
static const fl_log_sink_ops_t s_audit_ops = {audit_emit};
static fl_log_sink_t s_audit_sink = {&s_audit_ops, NULL};
static int audited(const char *needle) {
    int hit;
    pthread_mutex_lock(&s_audit_mu);
    hit = strstr(s_audit, needle) != NULL;
    pthread_mutex_unlock(&s_audit_mu);
    return hit;
}
static char s_tmp[256];
static char s_home[1024];

/* The audit file is written in the working directory: capture from inside the test's own. */
static void audit_capture(int on) {
    pthread_mutex_lock(&s_audit_mu);
    s_audit[0] = '\0';
    pthread_mutex_unlock(&s_audit_mu);
    if (on) {
        if (!getcwd(s_home, sizeof s_home) || chdir(s_tmp) != 0)
            s_home[0] = '\0';
        (void)setenv(FL_AUDIT_ENV, "1", 1);
        fl_audit_set_sink(&s_audit_sink);
    } else {
        fl_audit_set_sink(NULL);
        unsetenv(FL_AUDIT_ENV);
        (void)unlink(FL_AUDIT_REL_DEFAULT);
        if (s_home[0])
            (void)chdir(s_home);
    }
}

/* Containers made by the driver, beside s_tmp/<name>: one head by author, two heads, or none. */
static int put_container(const char *name, const fl_kq_buf_t *c) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s%s", s_tmp, name, FL_KQ_CONTAINER_SUFFIX);
    return fl_fs_write_file_atomic(path, c->data, c->len);
}

static int make_containers(const char *author, const uint8_t key[32], kq_file_offer_t *offer) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    fl_kq_buf_t base = {0}, one = {0}, left = {0}, right = {0}, both = {0};
    uint8_t id[16], rev[32];
    int rc = -1;

    memset(id, 0x53, sizeof id);
    if (drv && drv->file_new(id, "doc.txt", author, &base) == KQ_FILE_OK &&
        drv->check_in(&base, author, "2026-11-10T09:00:00.000Z", (const uint8_t *)"v1\n", 3, key, &one, rev) == KQ_FILE_OK &&
        drv->offer(&one, rev, offer) == KQ_FILE_OK &&
        drv->check_in(&one, author, "2026-11-10T09:01:00.000Z", (const uint8_t *)"a\n", 2, key, &left, rev) == KQ_FILE_OK &&
        drv->check_in(&one, author, "2026-11-10T09:02:00.000Z", (const uint8_t *)"b\n", 2, key, &right, rev) == KQ_FILE_OK &&
        drv->merge(&left, &right, author, "2026-11-10T09:03:00.000Z", &both) == KQ_FILE_OK &&
        put_container("single.txt", &one) == 0 && put_container("forked.txt", &both) == 0 &&
        put_container("empty.txt", &base) == 0)
        rc = 0;
    fl_kq_buf_free(&base);
    fl_kq_buf_free(&one);
    fl_kq_buf_free(&left);
    fl_kq_buf_free(&right);
    fl_kq_buf_free(&both);
    return rc;
}

typedef struct {
    fl_kq_autosign_file_t file; /* first: what fl_kq_autosign_head_author reads */
    char path[512];
    _Atomic int signed_count;
    char signed_as[FL_HERITAGE_KEY_ID_MAX];
} real_obj_t;

static int64_t real_last(void *c) { (void)c; return 0; }
static fl_result_t real_sign(void *c, const char *author, const uint8_t key[32], kq_signing_mode_t m) {
    real_obj_t *o = (real_obj_t *)c;
    (void)key; (void)m;
    snprintf(o->signed_as, sizeof o->signed_as, "%s", author);
    atomic_fetch_add(&o->signed_count, 1);
    return FL_RESULT_OK;
}

static void real_init(real_obj_t *o, const char *name) {
    snprintf(o->path, sizeof o->path, "%s/%s", s_tmp, name);
    o->file.path = o->path;
}

/* KQ-20: the production head-author callback asks KeyQuorum. The single head's author is signed for;
 * a forked file is audited kq-autosign-forked and never signed; an empty or missing container names
 * nobody. */
static int test_autosign_asks_keyquorum_for_the_head_author(void) {
    static real_obj_t single, forked, empty, missing;
    const fl_user_record_t *flin = account("Flinstone");
    fl_kq_autosign_t spec = {.last_signature_sec = real_last, .head_author = fl_kq_autosign_head_author,
                             .sign = real_sign, .interval_sec = 1};
    kq_file_offer_t offer;
    uint8_t key[32];
    char who[FL_HERITAGE_KEY_ID_MAX];

    ASSERT(flin);
    ASSERT(fl_user_db_open_key(fl_session_user_db(), "Flinstone", "flinstone", key) == FL_RESULT_OK);
    ASSERT(make_containers(flin->key_id, key, &offer) == 0);
    mem_domain_secure_zero(key, sizeof key);
    real_init(&single, "single.txt");
    real_init(&forked, "forked.txt");
    real_init(&empty, "empty.txt");
    real_init(&missing, "missing.txt");

    ASSERT(fl_kq_autosign_head_author(&single, who, sizeof who) == FL_RESULT_OK && strcmp(who, flin->key_id) == 0);
    ASSERT(fl_kq_autosign_head_author(&forked, who, sizeof who) == FL_RESULT_BUSY && who[0] == '\0');
    ASSERT(fl_kq_autosign_head_author(&empty, who, sizeof who) != FL_RESULT_OK);
    ASSERT(fl_kq_autosign_head_author(&empty, who, sizeof who) != FL_RESULT_BUSY && who[0] == '\0');
    ASSERT(fl_kq_autosign_head_author(&missing, who, sizeof who) != FL_RESULT_OK);
    ASSERT(fl_kq_autosign_head_author(&missing, who, sizeof who) != FL_RESULT_BUSY && who[0] == '\0');
    ASSERT(fl_kq_autosign_head_author(NULL, who, sizeof who) == FL_RESULT_INVAL);

    audit_capture(1);
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_OK);
    spec.label = "single-real";  spec.ctx = &single;  ASSERT(fl_kq_service_autosign_add(&spec) == FL_RESULT_OK);
    spec.label = "forked-real";  spec.ctx = &forked;  ASSERT(fl_kq_service_autosign_add(&spec) == FL_RESULT_OK);
    spec.label = "empty-real";   spec.ctx = &empty;   ASSERT(fl_kq_service_autosign_add(&spec) == FL_RESULT_OK);
    sleep_ms(2600);
    ASSERT(atomic_load(&single.signed_count) >= 1 && strcmp(single.signed_as, flin->key_id) == 0);
    ASSERT(atomic_load(&forked.signed_count) == 0);
    ASSERT(atomic_load(&empty.signed_count) == 0);
    ASSERT(audited("event=kq-autosign-forked subject=forked-real"));
    ASSERT(audited("event=kq-autosign subject=single-real"));
    ASSERT(!audited("subject=empty-real"));
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    audit_capture(0);
    return 0;
}

/* KQ-12: a refused receive leaves a KQER signed by the account that is online, named by the offer's
 * SHA-256, inside the held landing directory; it verifies only for that artifact, and nothing is
 * written when nobody is online or no directory is held. */
static void audit_clear(void) {
    pthread_mutex_lock(&s_audit_mu);
    s_audit[0] = '\0';
    pthread_mutex_unlock(&s_audit_mu);
}

/* Bytes 5..7 of a KQER: the code (u16 big-endian) and the class (src/error_report.rs). */
static int report_code_class(const char *path, unsigned *code, unsigned *class_code) {
    uint8_t head[8];
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f)
        return -1;
    n = fread(head, 1, sizeof head, f);
    fclose(f);
    if (n != sizeof head || memcmp(head, "KQER", 4) != 0)
        return -1;
    *code = ((unsigned)head[5] << 8) | head[6];
    *class_code = head[7];
    return 0;
}

static int test_a_refusal_is_reported_as_the_online_account(void) {
    static const struct {
        fl_result_t why;
        unsigned code, class_code;
    } rows[] = {
        {FL_RESULT_INVAL, 1u, 1u},
        {FL_RESULT_NOENT, 2u, 2u},
        {FL_RESULT_ACCES, 8u, 5u},
        {FL_RESULT_NOMEM, 8u, 7u},
    };
    static const fl_result_t others[] = {FL_RESULT_OK, FL_RESULT_ERR, FL_RESULT_BUSY, FL_RESULT_NOSYS};
    const fl_user_record_t *flin = account("Flinstone");
    fl_user_db_keydir_t keys;
    fl_kq_dir_t held, gone;
    kq_file_offer_t offer, other, each;
    fl_kq_buf_t lines = {0};
    uint8_t key[32], ref[32], other_ref[32], each_ref[32];
    char single[512], report[512], other_report[512], each_report[512], missing[512];
    unsigned code = 0, class_code = 0, i;
    size_t events = 0;

    ASSERT(flin);
    ASSERT(fl_user_db_open_key(fl_session_user_db(), "Flinstone", "flinstone", key) == FL_RESULT_OK);
    ASSERT(make_containers(flin->key_id, key, &offer) == 0);
    mem_domain_secure_zero(key, sizeof key);
    snprintf(single, sizeof single, "%s/single.txt", s_tmp);
    ASSERT(fl_kq_cp_offer_ref(&offer, ref) == FL_RESULT_OK);
    other = offer;
    other.revision_id[0] ^= 1u;
    ASSERT(fl_kq_cp_offer_ref(&other, other_ref) == FL_RESULT_OK);
    ASSERT(fl_kq_cp_report_path(s_tmp, ref, report, sizeof report) == 0);
    ASSERT(fl_kq_cp_report_path(s_tmp, other_ref, other_report, sizeof other_report) == 0);
    unlink(report);
    ASSERT(fl_kq_cp_storage()->dir_open(NULL, s_tmp, &held) == 0);

    /* nobody online: nothing is signed or written, and that is audited */
    audit_capture(1);
    fl_kq_report_refusal_in(&offer, FL_RESULT_ACCES, &held);
    ASSERT(access(report, F_OK) != 0);
    ASSERT(audited("event=kq-report-offline"));

    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_OK);
    /* the declared entry point has no held directory: it writes nothing, never a guessed path */
    fl_kq_report_refusal(&offer, FL_RESULT_ACCES);
    ASSERT(access(report, F_OK) != 0);
    ASSERT(audited("event=kq-report-uncontained"));
    /* a local problem is not a refusal of the letter */
    for (i = 0; i < sizeof others / sizeof others[0]; i++) {
        fl_kq_report_refusal_in(&offer, others[i], &held);
        ASSERT(access(report, F_OK) != 0);
    }
    fl_kq_report_refusal_in(&offer, FL_RESULT_ACCES, &held);
    ASSERT(access(report, F_OK) == 0 && access(other_report, F_OK) != 0);
    ASSERT(audited("event=kq-report subject=receive"));
    /* the same offer again: not rewritten, not counted */
    fl_kq_report_refusal_in(&offer, FL_RESULT_INVAL, &held);
    ASSERT(report_code_class(report, &code, &class_code) == 0 && code == 8u && class_code == 5u);
    ASSERT(audited("event=kq-report-exists"));
    /* every row of the table: the code and class it names */
    each = offer;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        each.policy_hash[0] = (uint8_t)(0xA0 + i);
        ASSERT(fl_kq_cp_offer_ref(&each, each_ref) == FL_RESULT_OK);
        ASSERT(fl_kq_cp_report_path(s_tmp, each_ref, each_report, sizeof each_report) == 0);
        fl_kq_report_refusal_in(&each, rows[i].why, &held);
        ASSERT(report_code_class(each_report, &code, &class_code) == 0);
        ASSERT(code == rows[i].code && class_code == rows[i].class_code);
    }
    /* a directory that cannot be written: audited as failed, and the caller's refusal stands as
     * it was (the entry point returns nothing that could change it) */
    snprintf(missing, sizeof missing, "%s/no-such-dir", s_tmp);
    gone.fd = -1;
    gone.dir = missing;
    audit_clear();
    fl_kq_report_refusal_in(&other, FL_RESULT_ACCES, &gone);
    ASSERT(audited("event=kq-report-failed"));
    ASSERT(access(other_report, F_OK) != 0);
    /* past the cap: nothing written, audited, no report text or ref in the audit */
    each = offer;
    for (i = 0; i < FL_KQ_REPORT_CAP; i++) {
        each.policy_hash[1] = (uint8_t)(0x01 + i);
        fl_kq_report_refusal_in(&each, FL_RESULT_ACCES, &held);
    }
    sleep_ms(1100); /* the audit sink is rate limited per second (FL_LOG_RL_MAX_PER_SEC) */
    audit_clear();
    fl_kq_report_refusal_in(&other, FL_RESULT_ACCES, &held);
    ASSERT(access(other_report, F_OK) != 0);
    ASSERT(audited("event=kq-report-capped subject=receive"));
    {
        char hex[65];
        fl_hex_encode(other_ref, 32, hex);
        ASSERT(!audited(hex));
    }
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    audit_capture(0);
    fl_kq_cp_storage()->dir_close(NULL, &held);

    /* KeyQuorum accepts it for this artifact and this reporter, and records one ErrorReported */
    fl_user_db_keydir_init(&keys, fl_session_user_db());
    ASSERT(fl_kq_cp_inject_report_with(&keys.base, flin->key_id, report, other_ref, single) == FL_KQ_CP_BAD_SIGNATURE);
    ASSERT(fl_kq_cp_inject_report_with(&keys.base, flin->key_id, report, ref, single) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_history(single, &lines) == FL_KQ_CP_OK);
    for (size_t j = 0; j + 4 <= lines.len; j++)
        if (memcmp(lines.data + j, "\t48\t", 4) == 0)
            events++;
    fl_kq_buf_free(&lines);
    ASSERT(events == 1);
    unlink(report);
    return 0;
}

/* The service queue has no executor: it polls, and it consumes no durable record. */
static int test_the_private_queue_consumes_no_durable_record(void) {
    fl_wq_work_t w = {.fn = count_job, .pq_layer = FL_WQ_LAYER_URGENT};
    fl_wq_journal_record_t r;
    uint64_t id;
    int ex;

    fl_wq_journal_mem_reset();
    memset(&r, 0, sizeof r);
    r.executor = FL_WQ_EXECUTOR_DEFAULT;
    r.tag = 0x7A01;
    r.layer = FL_WQ_LAYER_URGENT;
    r.max_attempts = FL_WQ_MAX_ATTEMPTS;
    r.idem_key[0] = 0x4B;
    ASSERT(fl_wq_journal_mem()->append(NULL, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_OK);
    atomic_store(&s_ran, 0);
    ASSERT(fl_kq_service_submit(&w) == FL_RESULT_OK);
    for (int i = 0; i < 100 && atomic_load(&s_ran) == 0; i++)
        sleep_ms(20);
    ASSERT(atomic_load(&s_ran) == 1); /* the service polled */
    sleep_ms(100);
    ASSERT(fl_wq_journal_mem()->load(NULL, id, &r) == FL_RESULT_OK && r.state == FL_WQ_ST_PENDING);
    fl_wq_journal_mem_reset();
    return 0;
}

static int test_orderly_shutdown_is_the_only_end(void) {
    fl_wq_work_t w = {.fn = count_job, .pq_layer = FL_WQ_LAYER_BACKGROUND};

    atomic_store(&s_ran, 0);
    ASSERT(fl_session_login("Flinstone", "flinstone") == FL_RESULT_OK);
    ASSERT(fl_kq_service_online_count() == 1);
    ASSERT(fl_kq_service_submit_every(&w, 3600ll * 1000000000ll) == FL_RESULT_OK); /* due in an hour */
    fl_kq_service_shutdown();
    ASSERT(atomic_load(&s_ran) == 1);          /* protected work runs once at shutdown */
    ASSERT(!fl_kq_service_alive());
    ASSERT(fl_kq_service_online_count() == 0); /* and every key is wiped */
    ASSERT(fl_kq_service_submit(&w) == FL_RESULT_INVAL);
    return 0;
}

/* An account made before account keys existed has none: its first password login provisions one, and that key
 * must come online with the login. The online hook takes the record as it is once the key is saved. */
static int test_a_keyless_account_comes_online_at_its_first_login(void) {
    fl_user_db_t *db = fl_session_user_db_mut();
    fl_user_record_t *rec;
    int before;

    ASSERT(db && fl_user_db_add_user(db, "Keyless", "keyless-pw", 97, 0) == FL_RESULT_OK);
    rec = (fl_user_record_t *)fl_user_db_find(db, "Keyless"); /* test-only: the record as an old store has it */
    ASSERT(rec);
    rec->has_key = 0;
    before = (int)fl_kq_service_online_count();
    ASSERT(fl_session_login("Keyless", "keyless-pw") == FL_RESULT_OK);
    ASSERT((int)fl_kq_service_online_count() == before + 1);
    ASSERT(fl_session_logout() == FL_RESULT_OK);
    ASSERT((int)fl_kq_service_online_count() == before);
    return 0;
}

int main(void) {
    char tmpl[] = "/tmp/fl_kq_serviceXXXXXX";
    char path[300];
    int rc = 0;

    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
    if (!mkdtemp(tmpl))
        return 1;
    snprintf(path, sizeof path, "%s/users.db", tmpl);
    snprintf(s_tmp, sizeof s_tmp, "%s", tmpl);
    (void)setenv("FL_USERS_DB_PATH", path, 1);
#define RUN(t) do { if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } else printf("%s... OK\n", #t); } while (0)
    RUN(test_login_brings_the_key_online_and_logout_wipes_it);
    if (!rc) RUN(test_kqst_login_commits_only_after_token_open);
    if (!rc) RUN(test_a_kqst_account_cannot_log_in_by_password_alone);
    if (!rc) RUN(test_kqst_open_refusal_matrix);
    if (!rc) RUN(test_a_logout_that_overlaps_the_key_derivation_wins);
    if (!rc) RUN(test_the_thread_cannot_be_cancelled_or_signalled);
    if (!rc) RUN(test_protected_work_is_never_dropped_and_a_stall_fails_closed);
    if (!rc) RUN(test_nobody_may_control_the_service);
    if (!rc) RUN(test_the_periodic_sweep);
    if (!rc) RUN(test_autosign_asks_keyquorum_for_the_head_author);
    if (!rc) RUN(test_a_refusal_is_reported_as_the_online_account);
    if (!rc) RUN(test_the_private_queue_consumes_no_durable_record);
    if (!rc) RUN(test_a_keyless_account_comes_online_at_its_first_login);
    if (!rc) RUN(test_orderly_shutdown_is_the_only_end);
    unlink(path);
    {
        static const char *const left[] = {"single.txt.kqtf", "forked.txt.kqtf", "empty.txt.kqtf"};
        char p[512];
        for (size_t i = 0; i < sizeof left / sizeof left[0]; i++) {
            snprintf(p, sizeof p, "%s/%s", tmpl, left[i]);
            unlink(p);
        }
    }
    rmdir(tmpl);
    if (rc == 0)
        printf("All KeyQuorum key service tests passed.\n");
    return rc;
}
