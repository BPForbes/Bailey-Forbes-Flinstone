#include "session.h"
#include "elevation.h"
#include "fl/driver/keyquorum.h"
#include "fl/kq.h"
#include "mem_domain.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fl_user_db_t s_db;
static char s_current[FL_USER_NAME_MAX];
static fl_elevation_token_t s_active_elev = FL_ELEVATION_TOKEN_NONE;
static int s_sudo_scope_depth;
static int s_db_ready;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t s_session_mu = PTHREAD_MUTEX_INITIALIZER;
static fl_session_online_fn s_online_hook;
static fl_session_online_opened_fn s_opened_hook;
static fl_session_offline_fn s_offline_hook;
static uint64_t s_login_gen = 1; /* every login and logout starts a new generation; 0 is never one */
static void (*s_kqst_pre_open)(void); /* tests only: runs after unlock, before token open */

void fl_session_set_login_hooks(fl_session_online_fn online, fl_session_offline_fn offline,
                                fl_session_online_opened_fn opened) {
    s_online_hook = online;
    s_offline_hook = offline;
    s_opened_hook = opened;
}

void fl_session_test_set_kqst_pre_open(void (*fn)(void)) {
    s_kqst_pre_open = fn;
}

static void session_load_once(void) {
    const char *path = getenv("FL_USERS_DB_PATH");

    /* Seeded accounts call the driver before sh.c reaches drivers_init. */
    (void)fl_kq_driver_get_or_register();

    if (!path || !path[0])
        path = FL_SESSION_USERS_PATH;
    pthread_mutex_lock(&s_session_mu);
    if (fl_user_db_load(&s_db, path) != FL_RESULT_OK) {
        fl_user_db_seed_defaults(&s_db);
        strncpy(s_db.path, path, sizeof(s_db.path) - 1);
        (void)fl_user_db_save(&s_db);
    }
    strncpy(s_current, s_db.default_user, sizeof(s_current) - 1);
    s_current[sizeof(s_current) - 1] = '\0';
    s_db_ready = 1;
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_init(void) {
    pthread_once(&s_once, session_load_once);
}

const char *fl_session_current_user(void) {
    static _Thread_local char tls_current[FL_USER_NAME_MAX];

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    strncpy(tls_current, s_current, sizeof(tls_current) - 1);
    tls_current[sizeof(tls_current) - 1] = '\0';
    pthread_mutex_unlock(&s_session_mu);
    return tls_current;
}

const char *fl_session_current_key_id(void) {
    static _Thread_local char tls_key[FL_HERITAGE_KEY_ID_MAX];
    const fl_user_record_t *u;

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    u = fl_user_db_find(&s_db, s_current);
    if (u)
        strncpy(tls_key, u->key_id, sizeof(tls_key) - 1);
    else
        tls_key[0] = '\0';
    tls_key[sizeof(tls_key) - 1] = '\0';
    pthread_mutex_unlock(&s_session_mu);
    return tls_key;
}

int fl_session_is_elevated_account(void) {
    int elevated;

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    elevated = fl_user_db_is_elevated_user(&s_db, s_current);
    pthread_mutex_unlock(&s_session_mu);
    return elevated;
}

int fl_session_in_sudo_scope(void) {
    int in_scope;

    pthread_mutex_lock(&s_session_mu);
    in_scope = s_sudo_scope_depth > 0;
    pthread_mutex_unlock(&s_session_mu);
    return in_scope;
}

void fl_session_begin_sudo_scope(void) {
    pthread_mutex_lock(&s_session_mu);
    s_sudo_scope_depth++;
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_end_sudo_scope(void) {
    pthread_mutex_lock(&s_session_mu);
    if (s_sudo_scope_depth > 0)
        s_sudo_scope_depth--;
    pthread_mutex_unlock(&s_session_mu);
}

int fl_session_jail_privileged(void) {
    int privileged;

    pthread_mutex_lock(&s_session_mu);
    privileged = s_sudo_scope_depth > 0;
    if (!privileged && s_db_ready)
        privileged = fl_user_db_is_elevated_user(&s_db, s_current);
    pthread_mutex_unlock(&s_session_mu);
    return privileged;
}

int fl_session_has_elevation(void) {
    int has;

    pthread_mutex_lock(&s_session_mu);
    if (s_sudo_scope_depth > 0)
        has = 1;
    else if (fl_elevation_active(s_active_elev))
        has = 1;
    else if (!s_db_ready)
        has = 0;
    else
        has = fl_user_db_is_elevated_user(&s_db, s_current);
    pthread_mutex_unlock(&s_session_mu);
    return has;
}

/* An account marked password-plus-KQST must open its slot token (FL-15). Called with s_session_mu held
 * only to read the mode; the open itself runs after the lock is released. Wrong password already failed
 * before this path, so a missing token does not teach the mode to a stranger. */
static int kqst_mode(const char *name) {
    const fl_user_record_t *u = fl_user_db_find(&s_db, name);

    return u && u->auth_mode == FL_USER_AUTH_PASSWORD_KQST;
}

/* Called with s_session_mu held, after the password verified. */
static void login_ensure_account_key(const char *name, const char *password) {
    int created = 0;

    /*
     * An account created before account keys has none: the password just
     * verified seals a new one. A failure here does not block the login; the
     * account simply cannot sign bridges until a later login succeeds.
     */
    if (fl_user_db_ensure_key(&s_db, name, password, &created) != FL_RESULT_OK) {
        fprintf(stderr, "login: could not create a signing key for %s\n", name);
    } else if (created && fl_user_db_save(&s_db) != FL_RESULT_OK) {
        /* Never keep a key the store does not hold: the next login tries again. */
        fl_user_db_forget_created(&s_db, name, created);
        fprintf(stderr, "login: could not save the new signing key for %s\n", name);
    }
}

fl_result_t fl_session_login(const char *name, const char *password) {
    fl_result_t rc = FL_RESULT_ACCES;
    fl_user_record_t snapshot;
    uint64_t gen = 0;
    int have_snapshot = 0;

    fl_session_init();
    if (!name || !name[0] || !password)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    if (fl_user_db_verify_password(&s_db, name, password)) {
        const fl_user_record_t *u = fl_user_db_find(&s_db, name);
        int need_kqst = kqst_mode(name);

        if (u) {
            snapshot = *u; /* copy under the lock; KQST commit refreshes after open */
            have_snapshot = 1;
        }
        if (need_kqst) {
            const char *open_as = have_snapshot ? snapshot.name : name;
            uint8_t signing_secret[32];
            int open_st;
            fl_result_t pub_rc = FL_RESULT_ACCES;

            /* Do not expose s_current until the token opens. Password is checked; session is not. */
            pthread_mutex_unlock(&s_session_mu);
            if (s_kqst_pre_open)
                s_kqst_pre_open();
            open_st = fl_kq_open_account_secrets(open_as, password, NULL, signing_secret);
            if (open_st != FL_KQ_ACCOUNT_OK) {
                mem_domain_secure_zero(signing_secret, sizeof signing_secret);
                fprintf(stderr, "login: %s requires its KQST slot token; open refused\n", open_as);
                return FL_RESULT_ACCES;
            }
            pthread_mutex_lock(&s_session_mu);
            /* Re-check under the lock: the account must still be KQST and the password still match. */
            if (!fl_user_db_verify_password(&s_db, open_as, password) || !kqst_mode(open_as)) {
                pthread_mutex_unlock(&s_session_mu);
                mem_domain_secure_zero(signing_secret, sizeof signing_secret);
                fprintf(stderr, "login: %s requires its KQST slot token; open refused\n", open_as);
                return FL_RESULT_ACCES;
            }
            u = fl_user_db_find(&s_db, open_as);
            fl_elevation_revoke_all();
            strncpy(s_current, u ? u->name : open_as, sizeof(s_current) - 1);
            s_current[sizeof(s_current) - 1] = '\0';
            s_active_elev = FL_ELEVATION_TOKEN_NONE;
            s_sudo_scope_depth = 0;
            gen = ++s_login_gen;
            if (u) {
                snapshot = *u;
                have_snapshot = 1;
            } else {
                have_snapshot = 0;
            }
            pthread_mutex_unlock(&s_session_mu);
            if (s_opened_hook && have_snapshot)
                pub_rc = s_opened_hook(&snapshot, signing_secret, gen);
            mem_domain_secure_zero(signing_secret, sizeof signing_secret);
            if (pub_rc != FL_RESULT_OK) {
                pthread_mutex_lock(&s_session_mu);
                if (s_login_gen == gen) {
                    s_current[0] = '\0';
                    s_login_gen++;
                }
                pthread_mutex_unlock(&s_session_mu);
                fprintf(stderr, "login: %s requires its KQST slot token; open refused\n", open_as);
                return FL_RESULT_ACCES;
            }
            return FL_RESULT_OK;
        }
        if (!need_kqst)
            login_ensure_account_key(name, password);
        fl_elevation_revoke_all();
        /* Store the account's own spelling: "login root" signs in as Root. */
        strncpy(s_current, u ? u->name : name, sizeof(s_current) - 1);
        s_current[sizeof(s_current) - 1] = '\0';
        s_active_elev = FL_ELEVATION_TOKEN_NONE;
        s_sudo_scope_depth = 0;
        gen = ++s_login_gen;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_session_mu);
    /* Password accounts: the key service opens the sealed key with the password just verified. */
    if (rc == FL_RESULT_OK && s_online_hook && have_snapshot)
        (void)s_online_hook(&snapshot, password, gen);
    return rc;
}

int fl_session_verify_password(const char *password) {
    int ok = 0;

    fl_session_init();
    if (!password)
        return 0;
    pthread_mutex_lock(&s_session_mu);
    ok = fl_user_db_verify_password(&s_db, s_current, password) ? 1 : 0;
    pthread_mutex_unlock(&s_session_mu);
    return ok;
}

fl_result_t fl_session_set_user(const char *name) {
    const fl_user_record_t *u;
    fl_result_t rc = FL_RESULT_NOENT;

    fl_session_init();
    if (!name || !name[0])
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    u = fl_user_db_find(&s_db, name);
    if (u) {
        fl_elevation_revoke_all();
        strncpy(s_current, u->name, sizeof(s_current) - 1);
        s_current[sizeof(s_current) - 1] = '\0';
        s_active_elev = FL_ELEVATION_TOKEN_NONE;
        s_sudo_scope_depth = 0;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}

fl_result_t fl_session_grant_elevation(const char *reason, fl_elevation_token_t *out) {
    fl_result_t rc;

    fl_session_init();
    if (!out)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    if (fl_user_db_is_elevated_user(&s_db, s_current)) {
        *out = FL_ELEVATION_TOKEN_NONE;
        pthread_mutex_unlock(&s_session_mu);
        return FL_RESULT_OK;
    }
    rc = fl_elevation_grant(s_current, reason, out);
    if (rc == FL_RESULT_OK)
        s_active_elev = *out;
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}

void fl_session_bind_elevation(fl_elevation_token_t token) {
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    s_active_elev = token;
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_clear_elevation(void) {
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    if (s_active_elev != FL_ELEVATION_TOKEN_NONE) {
        fl_elevation_revoke(s_active_elev);
        s_active_elev = FL_ELEVATION_TOKEN_NONE;
    }
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_drop_elevation(void) {
    fl_session_init();
    fl_session_clear_elevation();
    pthread_mutex_lock(&s_session_mu);
    s_sudo_scope_depth = 0;
    pthread_mutex_unlock(&s_session_mu);
}

fl_result_t fl_session_publish_if_current(uint64_t login_generation, fl_session_publish_fn publish, void *ctx) {
    fl_result_t rc;

    if (!publish)
        return FL_RESULT_INVAL;
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    rc = (login_generation != 0 && login_generation != s_login_gen) ? FL_RESULT_BUSY : publish(ctx);
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}

uint64_t fl_session_login_generation(void) {
    uint64_t g;

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    g = s_login_gen;
    pthread_mutex_unlock(&s_session_mu);
    return g;
}

fl_result_t fl_session_snapshot_account(const char *name, fl_user_record_t *out) {
    const fl_user_record_t *u;
    fl_result_t rc = FL_RESULT_NOENT;

    if (!name || !out)
        return FL_RESULT_INVAL;
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    u = fl_user_db_find(&s_db, name);
    if (u) {
        *out = *u;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}

fl_result_t fl_session_logout(void) {
    const char *def;
    char who[FL_USER_NAME_MAX];

    fl_session_init();
    fl_elevation_revoke_all();
    pthread_mutex_lock(&s_session_mu);
    strncpy(who, s_current, sizeof(who) - 1); /* the account that is signing out */
    who[sizeof(who) - 1] = '\0';
    s_sudo_scope_depth = 0;
    s_active_elev = FL_ELEVATION_TOKEN_NONE;
    s_login_gen++; /* a key still being derived for the login that ends here must not be published */
    def = s_db.default_user;
    if (!def || !def[0])
        def = "Flinstone";
    strncpy(s_current, def, sizeof(s_current) - 1);
    s_current[sizeof(s_current) - 1] = '\0';
    pthread_mutex_unlock(&s_session_mu);
    if (s_offline_hook)
        s_offline_hook(who); /* the service wipes that account's key */
    return FL_RESULT_OK;
}

fl_user_db_t *fl_session_user_db_mut(void) {
    fl_session_init();
    return &s_db;
}

const fl_user_db_t *fl_session_user_db(void) {
    fl_session_init();
    return &s_db;
}

fl_result_t fl_session_save_users(void) {
    fl_result_t rc;

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    rc = fl_user_db_save(&s_db);
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}
