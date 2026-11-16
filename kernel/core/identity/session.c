#include "session.h"
#include "elevation.h"
#include "fl/driver/keyquorum.h"
#include "fl/hex.h"
#include "fl/kq.h"
#include "fl/mem_asm.h"
#include "fl/random.h"
#include "mem_domain.h"
#include "timekeeping.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static fl_user_db_t s_db;
static char s_current[FL_USER_NAME_MAX];
static fl_elevation_token_t s_active_elev = FL_ELEVATION_TOKEN_NONE;
static int s_sudo_scope_depth;
static int s_db_ready;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t s_session_mu = PTHREAD_MUTEX_INITIALIZER;
static fl_session_online_fn s_online_hook;
static fl_session_online_opened_fn s_opened_hook;
static fl_session_online_box_fn s_box_hook;
static fl_session_offline_fn s_offline_hook;
static uint64_t s_login_gen = 1; /* every login and logout starts a new generation; 0 is never one */
static void (*s_kqst_pre_open)(void); /* tests only: runs after unlock, before token open */
static void (*s_switch_pre_lock)(void); /* tests only: before the final lock, mutex not held */
static void (*s_switch_pre_commit)(void); /* tests only: after recheck, both locks held */
static fl_session_audit_fn s_audit_hook;

void fl_session_set_login_box_hook(fl_session_online_box_fn box) {
    s_box_hook = box;
}

void fl_session_set_login_hooks(fl_session_online_fn online, fl_session_offline_fn offline,
                                fl_session_online_opened_fn opened) {
    s_online_hook = online;
    s_offline_hook = offline;
    s_opened_hook = opened;
}

void fl_session_test_set_kqst_pre_open(void (*fn)(void)) {
    s_kqst_pre_open = fn;
}

void fl_session_test_set_switch_pre_lock(void (*fn)(void)) {
    s_switch_pre_lock = fn;
}

void fl_session_test_set_switch_pre_commit(void (*fn)(void)) {
    s_switch_pre_commit = fn;
}

/* One character of a store path, or -1 when it may not be in one: letters, digits, '/', '.', '_',
 * '-' and '+'. Spaces, quotes, control bytes, '\\', '%', '~' and everything else are refused. */
static int store_path_char(int c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '.' ||
        c == '_' || c == '-' || c == '+')
        return c;
    return -1;
}

/*
 * Where the account store is, from FL_USERS_DB_PATH (the operator's to set; tests use it for a temporary
 * store). Every file the kernel keeps beside the store (its tokens, its kqst directory) is under the same
 * directory, so the value is checked once, here, before anything opens it (CodeQL cpp/path-injection on
 * PR 416): only the characters store_path_char takes, no ".." part, and it fits. The store is opened at
 * the copy built from the checked characters, never at the environment's string. 0 when it is unset,
 * 1 when out holds the checked path, -1 when it is refused.
 */
static int store_path_from_env(char *out, size_t cap) {
    const char *v = getenv("FL_USERS_DB_PATH");
    size_t i, n;

    if (!v || !v[0])
        return 0;
    n = strlen(v);
    if (n >= cap)
        return -1;
    for (i = 0; i < n; i++) {
        int c = store_path_char((unsigned char)v[i]);

        if (c < 0) {
            out[0] = '\0';
            return -1;
        }
        out[i] = (char)c;
    }
    out[n] = '\0';
    /* A ".." part: at the start, after a '/', and followed by '/' or the end. */
    for (i = 0; i + 1 < n; i++) {
        if (out[i] == '.' && out[i + 1] == '.' && (i == 0 || out[i - 1] == '/') &&
            (i + 2 == n || out[i + 2] == '/')) {
            out[0] = '\0';
            return -1;
        }
    }
    return 1;
}

static void session_load_once(void) {
    static char env_path[sizeof s_db.path];
    const char *path = FL_SESSION_USERS_PATH;
    fl_result_t rc;
    int from_env;

    /* Seeded accounts call the driver before sh.c reaches drivers_init. */
    (void)fl_kq_driver_get_or_register();

    from_env = store_path_from_env(env_path, sizeof env_path);
    if (from_env < 0) {
        /* Never fall back to another store: the session stays unavailable until the setting is fixed. */
        fprintf(stderr, "session: FL_USERS_DB_PATH is not a path the kernel uses (letters, digits, "
                        "'/', '.', '_', '-', '+'; no \"..\")\n");
        pthread_mutex_lock(&s_session_mu);
        s_db_ready = 0;
        s_current[0] = '\0';
        pthread_mutex_unlock(&s_session_mu);
        return;
    }
    if (from_env > 0)
        path = env_path;
    pthread_mutex_lock(&s_session_mu);
    rc = fl_user_db_load(&s_db, path);
    if (rc != FL_RESULT_OK) {
        /* Do not seed or save: a bad store must stay on disk, and the session stays unavailable. */
        s_db_ready = 0;
        s_current[0] = '\0';
    } else {
        strncpy(s_current, s_db.default_user, sizeof(s_current) - 1);
        s_current[sizeof(s_current) - 1] = '\0';
        s_db_ready = 1;
    }
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

/*
 * A password account that signs in gets its slot token from the kernel (fl_session_make_kqst): the
 * password it just gave seals the keys it already has. From its next sign-in on it uses the token.
 * A failure never blocks this sign-in; the next one tries again. Nothing is made where no driver
 * can seal a token (the freestanding image, a test stub) or no key service takes token sign-ins.
 * Called without s_session_mu.
 */
static void login_make_kqst(const fl_user_record_t *account, const char *password) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    int64_t now = 0;
    fl_result_t rc;

    if (!drv || !drv->slot_seal || account->auth_mode != FL_USER_AUTH_PASSWORD || !account->has_key ||
        !account->has_box)
        return;
    if (fl_time_wall_sec(&now) != FL_RESULT_OK)
        now = 0;
    rc = fl_session_make_kqst(account->name, password, now, NULL);
    if (rc != FL_RESULT_OK && rc != FL_RESULT_NOSYS)
        fprintf(stderr, "login: could not make a KQST slot token for %s; the next login tries again\n",
                account->name);
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
    if (!s_db_ready || !s_db.loaded) {
        pthread_mutex_unlock(&s_session_mu);
        return FL_RESULT_ERR;
    }
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
            uint8_t box_secret[32];
            int open_st;
            fl_result_t pub_rc = FL_RESULT_ACCES;
            uint64_t attempt_gen;

            /* Do not expose s_current until the token opens. Password is checked; session is not.
             * The generation taken here must still be the current one when the token has opened:
             * a logout or another login in the meantime supersedes this attempt. */
            attempt_gen = s_login_gen;
            pthread_mutex_unlock(&s_session_mu);
            if (s_kqst_pre_open)
                s_kqst_pre_open();
            open_st = fl_kq_open_account_keys(open_as, password, NULL, signing_secret, box_secret);
            if (open_st != FL_KQ_ACCOUNT_OK) {
                mem_domain_secure_zero(signing_secret, sizeof signing_secret);
                mem_domain_secure_zero(box_secret, sizeof box_secret);
                fprintf(stderr, "login: %s requires its KQST slot token; open refused\n", open_as);
                return FL_RESULT_ACCES;
            }
            pthread_mutex_lock(&s_session_mu);
            if (s_login_gen != attempt_gen) {
                /* A logout, or another login, ran while the token opened: it has the session now. */
                pthread_mutex_unlock(&s_session_mu);
                mem_domain_secure_zero(signing_secret, sizeof signing_secret);
                mem_domain_secure_zero(box_secret, sizeof box_secret);
                fprintf(stderr, "login: %s was superseded while its KQST slot token opened\n", open_as);
                return FL_RESULT_ACCES;
            }
            /* Re-check under the lock: the account must still be KQST and the password still match. */
            if (!fl_user_db_verify_password(&s_db, open_as, password) || !kqst_mode(open_as)) {
                pthread_mutex_unlock(&s_session_mu);
                mem_domain_secure_zero(signing_secret, sizeof signing_secret);
                mem_domain_secure_zero(box_secret, sizeof box_secret);
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
            if (pub_rc == FL_RESULT_OK && s_box_hook && have_snapshot)
                pub_rc = s_box_hook(&snapshot, box_secret, gen);
            mem_domain_secure_zero(signing_secret, sizeof signing_secret);
                mem_domain_secure_zero(box_secret, sizeof box_secret);
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
        if (!need_kqst) {
            login_ensure_account_key(name, password);
            /* The key may have just been provisioned and saved: the online hook must see the record as it
             * is now, or it would find no key and leave this account's key offline. */
            u = fl_user_db_find(&s_db, name);
            if (u) {
                snapshot = *u;
                have_snapshot = 1;
            }
        }
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
    if (rc == FL_RESULT_OK && have_snapshot)
        login_make_kqst(&snapshot, password);
    return rc;
}

/* s_session_mu is held. name is s_current or a caller name; it is not copied out. */
static int session_verify_password_locked(const char *name, const char *password) {
    if (!s_db_ready || !s_db.loaded || !name || !password)
        return 0;
    return fl_user_db_verify_password(&s_db, name, password) ? 1 : 0;
}

int fl_session_verify_password(const char *password) {
    int ok = 0;

    fl_session_init();
    if (!password)
        return 0;
    pthread_mutex_lock(&s_session_mu);
    ok = session_verify_password_locked(s_current, password);
    pthread_mutex_unlock(&s_session_mu);
    return ok;
}

/* Called with s_session_mu held: the identity commit that su, sudo -i and the restore after su -c
 * share. Every elevation ends with the old identity. */
static void commit_user_locked(const fl_user_record_t *u) {
    fl_elevation_revoke_all();
    strncpy(s_current, u->name, sizeof(s_current) - 1);
    s_current[sizeof(s_current) - 1] = '\0';
    s_active_elev = FL_ELEVATION_TOKEN_NONE;
    s_sudo_scope_depth = 0;
}

void fl_session_set_audit_hook(fl_session_audit_fn fn) {
    s_audit_hook = fn;
}

static void session_audit(const char *actor_key, const char *event, const char *subject_key,
                          const char *detail) {
    if (s_audit_hook)
        s_audit_hook(actor_key, event, subject_key, detail);
}

/* Takes name with no check; login_generation non-zero takes it only while that login is still the
 * current one (BUSY otherwise, audited, and the session is left as it is). */
static fl_result_t set_user_if_current(const char *name, uint64_t login_generation) {
    const fl_user_record_t *u, *cu;
    char cur_key[FL_HERITAGE_KEY_ID_MAX], name_key[FL_HERITAGE_KEY_ID_MAX];
    fl_result_t rc = FL_RESULT_NOENT;

    fl_session_init();
    if (!name || !name[0])
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    u = fl_user_db_find(&s_db, name);
    if (login_generation != 0 && login_generation != s_login_gen) {
        cu = fl_user_db_find(&s_db, s_current);
        snprintf(cur_key, sizeof cur_key, "%s", cu ? cu->key_id : "");
        snprintf(name_key, sizeof name_key, "%s", u ? u->key_id : "");
        pthread_mutex_unlock(&s_session_mu);
        session_audit(cur_key, "su-restore-refused", name_key, "superseded");
        return FL_RESULT_BUSY;
    }
    if (u) {
        commit_user_locked(u);
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}

fl_result_t fl_session_set_user(const char *name) {
    return set_user_if_current(name, 0);
}

fl_result_t fl_session_restore_user(const char *name, uint64_t login_generation) {
    if (login_generation == 0)
        return FL_RESULT_INVAL; /* 0 would mean unchecked: a restore always names its login */
    return set_user_if_current(name, login_generation);
}

int fl_session_verify_account_password(const char *name, const char *password) {
    int ok;

    fl_session_init();
    if (!name || !password)
        return 0;
    pthread_mutex_lock(&s_session_mu);
    ok = session_verify_password_locked(name, password);
    pthread_mutex_unlock(&s_session_mu);
    return ok;
}

/*
 * sudo is co-signed (owner decision on PR 416): Root's token, opened with Root's password, and the
 * invoking account's own key, opened with the invoker's password (its token for a KQST account,
 * else its sealed signing key), each sign the action under KeyQuorum's kq_sudo_sign with one fresh
 * nonce, and both signatures must verify under the public keys the account store holds before the
 * sudo is granted. Root invoking for itself needs no second signature. label is the audit's name for
 * the action ("sudo" or "sudo -i"); action is what is signed. NULL when co-signed (the audit detail
 * is written to detail: label, which invoker key signed, the nonce), else the reason word for the
 * audit. Called without s_session_mu.
 */
/* When a sudo window opened now would end (monotonic ns): the elevation token's own length. */
static int64_t window_end(void) {
    int64_t now = 0;

    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        return 0;
    return now + (int64_t)FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS * 1000000000LL;
}

static const char *cosign_sudo(const char *invoker_name, int invoker_kqst,
                               const char *invoker_password, const char *root_name,
                               const char *root_password, const char *label, const char *action,
                               int64_t hold_until, char *detail, size_t cap) {
    uint8_t nonce[16], inv_sig[64], root_sig[64];
    char nonce_hex[33];
    int st;

    if (!invoker_password || !fl_session_verify_account_password(invoker_name, invoker_password))
        return "invoker-password";
    /* hold_until > 0: the two keys are kept for the sudo window (FL-20 #399). */
    st = fl_kq_sudo_hold_open(invoker_name, invoker_password, invoker_kqst, root_name, root_password,
                              action, hold_until, nonce, inv_sig, root_sig);
    if (st == FL_KQ_COSIGN_AUTHORITY)
        return "kqst";
    if (st == FL_KQ_COSIGN_INVOKER)
        return invoker_kqst ? "invoker-kqst" : "invoker-key";
    if (st != FL_KQ_COSIGN_OK)
        return "cosign";
    fl_hex_encode(nonce, sizeof nonce, nonce_hex);
    nonce_hex[32] = '\0';
    /* Short enough for the audit log's detail field (63 characters). */
    snprintf(detail, cap, "%s; cosign=%s; nonce=%s", label, invoker_kqst ? "token" : "key", nonce_hex);
    return NULL;
}

/* A refused switch: audited (the two key ids and a reason word, never a password, key or token
 * byte). The terminal gets one message whatever the reason (the command prints it), so a refusal
 * says nothing about the policy or a bridge before a password is verified. Called without
 * s_session_mu. */
static fl_result_t switch_refused(fl_result_t rc, int sudo, const char *actor_key,
                                  const char *subject_key, const char *why) {
    session_audit(actor_key, sudo ? "sudo-refused" : "su-refused", subject_key, why);
    return rc;
}

fl_result_t fl_session_switch_user(const fl_session_switch_request_t *req) {
    const fl_user_record_t *u, *cu, *holder;
    char cur_name[FL_USER_NAME_MAX], cur_key[FL_HERITAGE_KEY_ID_MAX];
    char tgt_name[FL_USER_NAME_MAX], tgt_key[FL_HERITAGE_KEY_ID_MAX];
    char holder_name[FL_USER_NAME_MAX], holder_key[FL_HERITAGE_KEY_ID_MAX];
    char member_key[FL_HERITAGE_KEY_ID_MAX];
    char holder_hash[FL_PASSWORD_HASH_HEX_MAX], holder_salt[FL_PASSWORD_SALT_HEX_MAX];
    char facts_hex[65];
    char cosign_detail[96] = "";
    fl_kq_requirement_t need;
    uint64_t attempt_gen;
    int holder_auth, holder_kqst, cur_elevated, check_password = 1, proved = 0;
    int cur_auth, coauth = 0;
    const char *why = NULL;
    const char *pw;
    fl_result_t rc = FL_RESULT_OK;

    fl_session_init();
    mem_domain_secure_zero(holder_hash, sizeof holder_hash);
    mem_domain_secure_zero(holder_salt, sizeof holder_salt);
    mem_domain_secure_zero(facts_hex, sizeof facts_hex);
    member_key[0] = '\0';
    if (!req || !req->target || !req->target[0])
        return FL_RESULT_INVAL;
    pw = req->password;
    pthread_mutex_lock(&s_session_mu);
    u = fl_user_db_find(&s_db, req->target);
    cu = fl_user_db_find(&s_db, s_current);
    /* sudo asks for Root's key (keyquorum_access.h); su for the target's. */
    holder = req->sudo ? fl_user_db_find_by_key(&s_db, FL_HERITAGE_ROOT_KEY) : u;
    if (!u || !cu || !holder) {
        pthread_mutex_unlock(&s_session_mu);
        return FL_RESULT_NOENT;
    }
    snprintf(cur_name, sizeof cur_name, "%s", cu->name);
    snprintf(cur_key, sizeof cur_key, "%s", cu->key_id);
    snprintf(tgt_name, sizeof tgt_name, "%s", u->name);
    snprintf(tgt_key, sizeof tgt_key, "%s", u->key_id);
    snprintf(holder_name, sizeof holder_name, "%s", holder->name);
    snprintf(holder_key, sizeof holder_key, "%s", holder->key_id);
    memcpy(holder_hash, holder->password_hash, sizeof holder->password_hash);
    memcpy(holder_salt, holder->salt_hex, sizeof holder->salt_hex);
    holder_auth = holder->auth_mode;
    holder_kqst = holder_auth == FL_USER_AUTH_PASSWORD_KQST;
    cur_elevated = cu->is_elevated;
    cur_auth = cu->auth_mode;
    /* sudo -i is co-authorised by the invoker too, unless the invoker is Root itself. */
    coauth = req->sudo && strcmp(cur_key, holder_key) != 0;
    attempt_gen = s_login_gen;
    pthread_mutex_unlock(&s_session_mu);

    /* KeyQuorum names the factor; this side only collects it. The password KDF runs here, on the
     * copy taken above, so the final reservation does not derive it again. */
    need = fl_kq_unlock_requirement(cur_name, tgt_name, req->sudo);
    if (need.needs_parent_grant) {
        /* No way to collect a parent's grant exists on this side: refused, never skipped. */
        why = "parent-grant";
    } else {
        switch (need.factor) {
        case FL_KQ_ACCESS_NONE:
            check_password = 0;
            break;
        case FL_KQ_ACCESS_PASSWORD:
        case FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST:
            break;
        case FL_KQ_ACCESS_KQST_PASSWORD:
            /*
             * Owner decision (FL-12, #403): sudo -i always opens Root's token. A password-only Root
             * has none, so it is refused before any password is checked, whoever asks. For su, a
             * password-only holder keeps the rule from before KeyQuorum's (its password), audited
             * below as "su-kqst-legacy", never silent.
             */
            if (!holder_kqst && req->sudo)
                why = "no-root-kqst";
            break;
        default:
            why = "policy";
            break;
        }
    }
    if (!why && check_password && !fl_password_verify(pw, holder_salt, holder_hash))
        why = "password";
    if (!why && need.factor == FL_KQ_ACCESS_KQST_PASSWORD) {
        if (holder_kqst) {
            /* The holder's slot token, opened with the holder's password: a password alone never
             * does. A co-signed sudo opens it to sign (below), so it is opened once. */
            if (!coauth && fl_kq_open_account(holder_name, pw, NULL) != FL_KQ_ACCOUNT_OK)
                why = "kqst";
        } else {
            /* Only su gets here: sudo with a password-only Root was refused above. */
            session_audit(cur_key, "su-kqst-legacy", tgt_key, "password-only account");
        }
    }
    if (!why && coauth) {
        char action[FL_HERITAGE_KEY_ID_MAX + 16];

        snprintf(action, sizeof action, "sudo -i %s", tgt_key);
        why = cosign_sudo(cur_name, cur_auth == FL_USER_AUTH_PASSWORD_KQST, req->invoker_password,
                          holder_name, pw, "sudo -i", action, 0, cosign_detail, sizeof cosign_detail);
    }
    if (!why && need.factor == FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST) {
        fl_user_record_t member;

        if (!req->bridge_member || !req->bridge_member[0] || !req->bridge_member_password)
            why = "bridge-member";
        else if (fl_session_snapshot_account(req->bridge_member, &member) != FL_RESULT_OK ||
                 !member.key_id[0])
            why = "bridge-proof";
        else if (fl_kq_prove_bridge_member(need.bridge_uid, req->bridge_member,
                                           req->bridge_member_password, facts_hex) != FL_KQ_BRIDGE_OK)
            why = "bridge-proof";
        else {
            snprintf(member_key, sizeof member_key, "%s", member.key_id);
            proved = 1;
        }
    }
    if (why) {
        rc = switch_refused(FL_RESULT_ACCES, req->sudo, cur_key, tgt_key, why);
        goto wipe;
    }

    /* The factor is satisfied. A test may change the store here, before the reservation. */
    if (s_switch_pre_lock)
        s_switch_pre_lock();

    pthread_mutex_lock(&s_session_mu);
    {
        fl_user_bridge_hold_t *hold = NULL;
        fl_user_bridge_t *live = NULL;
        fl_user_bridge_t *now = NULL;
        fl_kq_requirement_t again;
        unsigned char sealed[FL_USER_BRIDGE_SEALED_MAX];
        unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN];
        char now_hex[65];
        size_t n = 0, slen = 0;
        fl_result_t hr;

        mem_domain_secure_zero(sealed, sizeof sealed);
        mem_domain_secure_zero(wrap_salt, sizeof wrap_salt);
        mem_domain_secure_zero(now_hex, sizeof now_hex);
        /* Session lock, then the writer slot. A taken slot is BUSY and does not commit. */
        hr = fl_user_db_bridge_reserve(&s_db, &hold);
        if (hr != FL_RESULT_OK) {
            why = hr == FL_RESULT_BUSY ? "busy" : "changed";
            rc = hr == FL_RESULT_BUSY ? FL_RESULT_BUSY : FL_RESULT_ACCES;
            goto release;
        }
        if (s_login_gen != attempt_gen || strcmp(s_current, cur_name) != 0) {
            why = "superseded";
            rc = FL_RESULT_BUSY;
            goto release;
        }
        u = fl_user_db_find(&s_db, tgt_name);
        cu = fl_user_db_find(&s_db, cur_name);
        holder = req->sudo ? fl_user_db_find_by_key(&s_db, FL_HERITAGE_ROOT_KEY) : u;
        if (!u || !cu || !holder || strcmp(u->key_id, tgt_key) != 0 ||
            strcmp(holder->key_id, holder_key) != 0 || holder->auth_mode != holder_auth ||
            cu->is_elevated != cur_elevated || (coauth && cu->auth_mode != cur_auth)) {
            why = "changed";
            rc = FL_RESULT_ACCES;
            goto release;
        }
        if (check_password &&
            (!asm_mem_ct_equal(holder->password_hash, holder_hash, sizeof holder->password_hash) ||
             !asm_mem_ct_equal(holder->salt_hex, holder_salt, sizeof holder->salt_hex))) {
            why = "changed";
            rc = FL_RESULT_ACCES;
            goto release;
        }
        /* The invoker's password that co-authorised it is still the account's. */
        if (coauth && !session_verify_password_locked(cur_name, req->invoker_password)) {
            why = "changed";
            rc = FL_RESULT_ACCES;
            goto release;
        }
        live = (fl_user_bridge_t *)calloc(FL_USER_BRIDGES_MAX, sizeof *live);
        if (!live) {
            why = proved ? "bridge-changed" : "changed";
            rc = FL_RESULT_ACCES;
            goto release;
        }
        /* A live-list error is a refusal: an empty list would lower the factor. */
        if (fl_user_db_bridge_live_held(hold, live, FL_USER_BRIDGES_MAX, &n) != FL_RESULT_OK) {
            why = proved ? "bridge-changed" : "changed";
            rc = FL_RESULT_ACCES;
            goto release;
        }
        again = fl_kq_requirement_from_snapshot(cu, u, req->sudo, live, n);
        if (again.factor != need.factor ||
            (again.needs_parent_grant ? 1 : 0) != (need.needs_parent_grant ? 1 : 0) ||
            (need.factor == FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST &&
             strcmp(again.bridge_uid, need.bridge_uid) != 0)) {
            why = proved ? "bridge-changed" : "changed";
            rc = FL_RESULT_ACCES;
            goto release;
        }
        if (proved) {
            now = (fl_user_bridge_t *)calloc(1, sizeof *now);
            if (!now ||
                fl_user_db_bridge_read_held(hold, need.bridge_uid, member_key, now, sealed,
                                            sizeof sealed, &slen, wrap_salt) != FL_RESULT_OK ||
                fl_kq_bridge_facts_hex(now, member_key, sealed, slen, wrap_salt, now_hex) !=
                    FL_RESULT_OK ||
                !asm_mem_ct_equal(now_hex, facts_hex, 64)) {
                why = "bridge-changed";
                rc = FL_RESULT_ACCES;
                goto release;
            }
        }
        if (s_switch_pre_commit)
            s_switch_pre_commit();
        commit_user_locked(u);
        why = NULL;
        rc = FL_RESULT_OK;
    release:
        mem_domain_secure_zero(sealed, sizeof sealed);
        mem_domain_secure_zero(wrap_salt, sizeof wrap_salt);
        mem_domain_secure_zero(now_hex, sizeof now_hex);
        free(now);
        free(live);
        fl_user_db_bridge_release(hold);
    }
    pthread_mutex_unlock(&s_session_mu);
    if (why)
        rc = switch_refused(rc, req->sudo, cur_key, tgt_key, why);
    else if (coauth)
        session_audit(cur_key, "sudo-coauth", holder_key, cosign_detail);
wipe:
    mem_domain_secure_zero(holder_hash, sizeof holder_hash);
    mem_domain_secure_zero(holder_salt, sizeof holder_salt);
    mem_domain_secure_zero(facts_hex, sizeof facts_hex);
    return rc;
}

int fl_session_sudo_needs_root(void) {
    const fl_user_record_t *root;
    int kqst;

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    root = fl_user_db_find_by_key(&s_db, FL_HERITAGE_ROOT_KEY);
    kqst = root && root->auth_mode == FL_USER_AUTH_PASSWORD_KQST;
    pthread_mutex_unlock(&s_session_mu);
    return kqst;
}

/* s_session_mu is held. The body of fl_session_grant_elevation. */
static fl_result_t grant_elevation_locked(const char *reason, fl_elevation_token_t *out) {
    fl_result_t rc;

    if (!out)
        return FL_RESULT_INVAL;
    if (!s_db_ready || !s_db.loaded)
        return FL_RESULT_ERR;
    if (fl_user_db_is_elevated_user(&s_db, s_current)) {
        *out = FL_ELEVATION_TOKEN_NONE;
        return FL_RESULT_OK;
    }
    rc = fl_elevation_grant(s_current, reason, out);
    if (rc == FL_RESULT_OK) {
        if (s_active_elev != FL_ELEVATION_TOKEN_NONE && s_active_elev != *out)
            fl_elevation_revoke(s_active_elev); /* a new sudo replaces the window: the clock starts again */
        s_active_elev = *out;
    }
    return rc;
}

fl_result_t fl_session_sudo_authenticate(const char *root_password, const char *invoker_password) {
    const fl_user_record_t *root, *cu;
    char cur_name[FL_USER_NAME_MAX], cur_key[FL_HERITAGE_KEY_ID_MAX], root_name[FL_USER_NAME_MAX];
    fl_kq_requirement_t need;
    fl_elevation_token_t tok = FL_ELEVATION_TOKEN_NONE;
    int64_t window = 0;
    uint64_t attempt_gen;
    int root_kqst, cur_auth, coauth;
    char cosign_detail[96] = "";
    const char *why = NULL;
    fl_result_t rc;

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    if (!s_db_ready || !s_db.loaded) {
        pthread_mutex_unlock(&s_session_mu);
        return FL_RESULT_ERR;
    }
    root = fl_user_db_find_by_key(&s_db, FL_HERITAGE_ROOT_KEY);
    cu = fl_user_db_find(&s_db, s_current);
    if (!root || !cu) {
        pthread_mutex_unlock(&s_session_mu);
        return FL_RESULT_ACCES;
    }
    snprintf(cur_name, sizeof cur_name, "%s", cu->name);
    snprintf(cur_key, sizeof cur_key, "%s", cu->key_id);
    snprintf(root_name, sizeof root_name, "%s", root->name);
    root_kqst = root->auth_mode == FL_USER_AUTH_PASSWORD_KQST;
    cur_auth = cu->auth_mode;
    coauth = strcmp(cur_key, root->key_id) != 0;
    attempt_gen = s_login_gen;
    pthread_mutex_unlock(&s_session_mu);

    need = fl_kq_unlock_requirement(cur_name, cur_name, 1);
    if (need.needs_parent_grant)
        why = "parent-grant";
    else if (need.factor != FL_KQ_ACCESS_KQST_PASSWORD)
        why = "policy";
    else if (!root_kqst)
        why = "no-root-kqst"; /* owner decision (FL-12, #403): no password-only fallback for sudo */
    else if (!fl_session_verify_account_password(root_name, root_password))
        why = "password";
    else if (!coauth && fl_kq_open_account(root_name, root_password, NULL) != FL_KQ_ACCOUNT_OK)
        why = "kqst";
    else if (coauth)
        why = cosign_sudo(cur_name, cur_auth == FL_USER_AUTH_PASSWORD_KQST, invoker_password,
                          root_name, root_password, "sudo", "sudo", window_end(), cosign_detail,
                          sizeof cosign_detail);
    if (why) {
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, why);
        return FL_RESULT_ACCES;
    }
    /* The checks ran without the session lock. A login or logout in that window changes the
     * generation. set_auth_mode and set_password do not, so both accounts' modes and the passwords
     * that were accepted are checked again here. The elevation token is taken for the account that
     * was checked, or not at all. */
    if (s_switch_pre_lock)
        s_switch_pre_lock();
    pthread_mutex_lock(&s_session_mu);
    if (s_login_gen != attempt_gen || strcmp(s_current, cur_name) != 0) {
        pthread_mutex_unlock(&s_session_mu);
        fl_kq_sudo_hold_release();
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, "superseded");
        return FL_RESULT_BUSY;
    }
    root = fl_user_db_find_by_key(&s_db, FL_HERITAGE_ROOT_KEY);
    cu = fl_user_db_find(&s_db, cur_name);
    if (!s_db_ready || !s_db.loaded || !root || !cu ||
        (root->auth_mode == FL_USER_AUTH_PASSWORD_KQST) != root_kqst || cu->auth_mode != cur_auth) {
        pthread_mutex_unlock(&s_session_mu);
        fl_kq_sudo_hold_release();
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, "mode");
        return FL_RESULT_ACCES;
    }
    if (!session_verify_password_locked(root_name, root_password)) {
        pthread_mutex_unlock(&s_session_mu);
        fl_kq_sudo_hold_release();
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, "password");
        return FL_RESULT_ACCES;
    }
    if (coauth && !session_verify_password_locked(cur_name, invoker_password)) {
        pthread_mutex_unlock(&s_session_mu);
        fl_kq_sudo_hold_release();
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, "invoker-password");
        return FL_RESULT_ACCES;
    }
    rc = grant_elevation_locked("sudo authentication", &tok);
    /* The kept keys end with the window the token opened, to the nanosecond. */
    if (rc == FL_RESULT_OK && coauth && fl_elevation_expires(tok, &window) == FL_RESULT_OK)
        fl_kq_sudo_hold_extend(window);
    else if (coauth)
        fl_kq_sudo_hold_release();
    pthread_mutex_unlock(&s_session_mu);
    if (rc == FL_RESULT_OK && coauth)
        session_audit(cur_key, "sudo-coauth", FL_HERITAGE_ROOT_KEY, cosign_detail);
    return rc;
}

/*
 * Pair the logged-in account with its slot token (KQST). The token must open with the account's
 * password and match the device id and public keys given (the ones keyquorum-device provision
 * printed); an account that already has a token must also open the one it has, so a password alone
 * never replaces a token. The row is written first and the mode saved after it; a failed save puts
 * the row and the mode back. Audited as "kqst-enrol" or "kqst-enrol-refused" with a reason word.
 */
fl_result_t fl_session_enroll_kqst(const char *password, const char *kqst_path,
                                   const unsigned char device_id[16],
                                   const unsigned char signing_public[32],
                                   const unsigned char encryption_public[32], int64_t minted_at,
                                   int64_t *generation_out) {
    const fl_user_record_t *cu;
    char cur_name[FL_USER_NAME_MAX], cur_key[FL_HERITAGE_KEY_ID_MAX], detail[48];
    fl_user_kqst_row_t *old = NULL;
    uint64_t attempt_gen;
    int64_t generation = 0;
    int cur_auth;
    const char *why = NULL;
    fl_result_t rc = FL_RESULT_ACCES;

    fl_session_init();
    if (!kqst_path || !kqst_path[0] || !device_id || !signing_public || !encryption_public)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    cu = (s_db_ready && s_db.loaded) ? fl_user_db_find(&s_db, s_current) : NULL;
    if (!cu) {
        pthread_mutex_unlock(&s_session_mu);
        return FL_RESULT_NOENT;
    }
    snprintf(cur_name, sizeof cur_name, "%s", cu->name);
    snprintf(cur_key, sizeof cur_key, "%s", cu->key_id);
    cur_auth = cu->auth_mode;
    attempt_gen = s_login_gen;
    pthread_mutex_unlock(&s_session_mu);

    if (!fl_session_verify_account_password(cur_name, password))
        why = "password";
    else if (cur_auth == FL_USER_AUTH_PASSWORD_KQST &&
             fl_kq_open_account(cur_name, password, NULL) != FL_KQ_ACCOUNT_OK)
        why = "old-kqst";
    else if (fl_kq_check_token(cur_key, password, kqst_path, device_id, signing_public,
                               encryption_public) != FL_KQ_ACCOUNT_OK)
        why = "kqst";
    if (why)
        goto refused;
    old = (fl_user_kqst_row_t *)calloc(1, sizeof *old);
    if (!old || fl_user_db_kqst_row_get(&s_db, cur_key, old) != FL_RESULT_OK) {
        why = "store";
        rc = FL_RESULT_ERR;
        goto refused;
    }
    if (s_switch_pre_lock)
        s_switch_pre_lock();
    pthread_mutex_lock(&s_session_mu);
    cu = (s_db_ready && s_db.loaded) ? fl_user_db_find(&s_db, cur_name) : NULL;
    if (s_login_gen != attempt_gen || strcmp(s_current, cur_name) != 0) {
        why = "superseded";
        rc = FL_RESULT_BUSY;
    } else if (!cu || strcmp(cu->key_id, cur_key) != 0 || cu->auth_mode != cur_auth ||
               !session_verify_password_locked(cur_name, password)) {
        why = "changed";
        rc = FL_RESULT_ACCES;
    } else if (fl_user_db_kqst_register(&s_db, cur_key, kqst_path, device_id, signing_public,
                                        encryption_public, minted_at, &generation) != FL_RESULT_OK) {
        why = "store";
        rc = FL_RESULT_ERR;
    } else if (cur_auth != FL_USER_AUTH_PASSWORD_KQST &&
               (fl_user_db_set_auth_mode(&s_db, cur_name, FL_USER_AUTH_PASSWORD_KQST) != FL_RESULT_OK ||
                fl_user_db_save(&s_db) != FL_RESULT_OK)) {
        /* Put both back: the account keeps logging in as it did. */
        (void)fl_user_db_set_auth_mode(&s_db, cur_name, cur_auth);
        (void)fl_user_db_kqst_row_restore(&s_db, cur_key, old);
        why = "store";
        rc = FL_RESULT_ERR;
    } else {
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_session_mu);
    if (why)
        goto refused;
    free(old);
    if (generation_out)
        *generation_out = generation;
    snprintf(detail, sizeof detail, "generation=%lld", (long long)generation);
    session_audit(cur_key, "kqst-enrol", cur_key, detail);
    return FL_RESULT_OK;
refused:
    free(old);
    session_audit(cur_key, "kqst-enrol-refused", cur_key, why);
    return rc;
}

/*
 * The kernel gives an account its token itself (owner decision on PR 416: no token path, device id
 * or keys typed by anyone). The account's own signing key and box key, opened with its password
 * from the account store, are sealed with that password to a fresh random device id into a KQST
 * kept beside the store (fl_kq_account_token_path), owner-only; the account keeps its keys. Then
 * the store's pairing row is written and the account switches to token sign-in, as kqst enroll
 * does, with the same rollback. Called without s_session_mu. Audited as "kqst-made" or
 * "kqst-make-refused" with a reason word.
 */
fl_result_t fl_session_make_kqst(const char *name, const char *password, int64_t minted_at,
                                 int64_t *generation_out) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    const fl_user_record_t *u;
    fl_user_record_t *snap = NULL;
    fl_user_kqst_row_t *old = NULL;
    char db_path[sizeof s_db.path], path[FL_KQ_TOKEN_PATH_MAX], detail[48];
    unsigned char dev[16], sig_pub[32], enc_pub[32];
    uint8_t sig[32], box[32];
    int64_t generation = 0;
    int wrote = 0, have_service = 0;
    const char *why = NULL;
    fl_result_t rc = FL_RESULT_ACCES;

    fl_session_init();
    if (!name || !name[0] || !password || !password[0])
        return FL_RESULT_INVAL;
    snap = (fl_user_record_t *)calloc(1, sizeof *snap);
    old = (fl_user_kqst_row_t *)calloc(1, sizeof *old);
    if (!snap || !old) {
        free(snap);
        free(old);
        return FL_RESULT_NOMEM;
    }
    pthread_mutex_lock(&s_session_mu);
    u = (s_db_ready && s_db.loaded) ? fl_user_db_find(&s_db, name) : NULL;
    if (u) {
        *snap = *u;
        snprintf(db_path, sizeof db_path, "%s", s_db.path[0] ? s_db.path : FL_USERS_DB_DEFAULT_PATH);
    }
    have_service = s_opened_hook != NULL;
    pthread_mutex_unlock(&s_session_mu);
    if (!u) {
        free(snap);
        free(old);
        return FL_RESULT_NOENT;
    }
    if (snap->auth_mode == FL_USER_AUTH_PASSWORD_KQST) {
        why = "exists";
    } else if (!drv || !drv->slot_seal || !drv->account_open) {
        why = "no-driver";
        rc = FL_RESULT_NOSYS;
    } else if (!have_service) {
        /* A token sign-in needs the key service to take the opened keys (it fails closed without
         * it), so a token made with no key service running would lock the account out. */
        why = "no-service";
        rc = FL_RESULT_NOSYS;
    } else if (!snap->has_key || !snap->has_box) {
        why = "no-key";
        rc = FL_RESULT_ERR;
    } else if (!fl_session_verify_account_password(snap->name, password)) {
        why = "password";
    } else if (drv->account_open(password, snap->signing_sealed, sizeof snap->signing_sealed, sig) !=
                   FL_KQ_ACCOUNT_OK ||
               drv->account_open(password, snap->box_sealed, sizeof snap->box_sealed, box) !=
                   FL_KQ_ACCOUNT_OK) {
        why = "key";
        rc = FL_RESULT_ERR;
    } else if (fl_kq_account_token_path(db_path, snap->key_id, path, sizeof path) != 0 ||
               fl_random_bytes(dev, sizeof dev) != 0) {
        why = "store";
        rc = FL_RESULT_ERR;
    } else if (fl_kq_write_account_token(snap->key_id, password, sig, box, dev, path, sig_pub,
                                         enc_pub) != FL_KQ_ACCOUNT_OK) {
        why = "seal";
        rc = FL_RESULT_ERR;
    } else {
        wrote = 1;
        /* The token holds the account's own keys: anything else is a fault, never a new identity. */
        if (!asm_mem_ct_equal(sig_pub, snap->signing_public, 32) ||
            !asm_mem_ct_equal(enc_pub, snap->box_public, 32)) {
            why = "seal";
            rc = FL_RESULT_ERR;
        }
    }
    mem_domain_secure_zero(sig, sizeof sig);
    mem_domain_secure_zero(box, sizeof box);
    if (why)
        goto refused;
    if (s_switch_pre_lock)
        s_switch_pre_lock();
    pthread_mutex_lock(&s_session_mu);
    u = (s_db_ready && s_db.loaded) ? fl_user_db_find(&s_db, snap->name) : NULL;
    if (!u || strcmp(u->key_id, snap->key_id) != 0 || u->auth_mode != snap->auth_mode ||
        !session_verify_password_locked(snap->name, password)) {
        why = "changed";
        rc = FL_RESULT_ACCES;
    } else if (fl_user_db_kqst_row_get(&s_db, snap->key_id, old) != FL_RESULT_OK) {
        why = "store";
        rc = FL_RESULT_ERR;
    } else if (fl_user_db_kqst_register(&s_db, snap->key_id, path, dev, sig_pub, enc_pub, minted_at,
                                        &generation) != FL_RESULT_OK) {
        why = "store";
        rc = FL_RESULT_ERR;
    } else if (fl_user_db_set_auth_mode(&s_db, snap->name, FL_USER_AUTH_PASSWORD_KQST) !=
                   FL_RESULT_OK ||
               fl_user_db_save(&s_db) != FL_RESULT_OK) {
        (void)fl_user_db_set_auth_mode(&s_db, snap->name, snap->auth_mode);
        (void)fl_user_db_kqst_row_restore(&s_db, snap->key_id, old);
        why = "store";
        rc = FL_RESULT_ERR;
    } else {
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_session_mu);
    if (why)
        goto refused;
    if (generation_out)
        *generation_out = generation;
    snprintf(detail, sizeof detail, "generation=%lld", (long long)generation);
    session_audit(snap->key_id, "kqst-made", snap->key_id, detail);
    mem_domain_secure_zero(snap, sizeof *snap);
    free(snap);
    free(old);
    return FL_RESULT_OK;
refused:
    /* A token nobody's pairing names is removed; an account that already had one is never here. */
    if (wrote)
        (void)unlink(path);
    session_audit(snap->key_id, "kqst-make-refused", snap->key_id, why);
    mem_domain_secure_zero(snap, sizeof *snap);
    free(snap);
    free(old);
    return rc;
}

fl_result_t fl_session_grant_elevation(const char *reason, fl_elevation_token_t *out) {
    fl_result_t rc;

    fl_session_init();
    if (!out)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    rc = grant_elevation_locked(reason, out);
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}

void fl_session_bind_elevation(fl_elevation_token_t token) {
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    s_active_elev = token;
    pthread_mutex_unlock(&s_session_mu);
}

/* s_session_mu is held. The same revoke fl_session_clear_elevation performs. */
static void revoke_bound_elevation_locked(void) {
    if (s_active_elev != FL_ELEVATION_TOKEN_NONE) {
        fl_elevation_revoke(s_active_elev);
        s_active_elev = FL_ELEVATION_TOKEN_NONE;
    }
    fl_kq_sudo_hold_release(); /* Root's key ends with the window */
}

void fl_session_clear_elevation(void) {
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    revoke_bound_elevation_locked();
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_drop_elevation(void) {
    fl_session_init();
    fl_session_clear_elevation();
    pthread_mutex_lock(&s_session_mu);
    s_sudo_scope_depth = 0;
    pthread_mutex_unlock(&s_session_mu);
}

fl_result_t fl_session_elevated_action(const char *action, int renew, int *deelevated) {
    char cur_name[FL_USER_NAME_MAX], cur_key[FL_HERITAGE_KEY_ID_MAX], detail[64], nonce_hex[33];
    char act[FL_KQ_SUDO_ACTION_MAX + 1];
    uint8_t nonce[16], inv_sig[64], root_sig[64];
    fl_elevation_token_t tok;
    uint64_t gen;
    int64_t now = 0, window = 0;
    int active, st;

    fl_session_init();
    if (deelevated)
        *deelevated = 0;
    if (!action || !action[0])
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    /* Root (and any account elevated by itself) acts with its own key: there is no window to sign. */
    if (!s_db_ready || !s_db.loaded || fl_user_db_is_elevated_user(&s_db, s_current)) {
        pthread_mutex_unlock(&s_session_mu);
        return FL_RESULT_OK;
    }
    tok = s_active_elev;
    active = fl_elevation_active(tok);
    snprintf(cur_name, sizeof cur_name, "%s", s_current);
    {
        const fl_user_record_t *cu = fl_user_db_find(&s_db, s_current);
        snprintf(cur_key, sizeof cur_key, "%s", cu ? cu->key_id : "");
    }
    gen = s_login_gen;
    if (!active && tok != FL_ELEVATION_TOKEN_NONE) {
        s_active_elev = FL_ELEVATION_TOKEN_NONE; /* the window ended: the user's own key from here */
        fl_kq_sudo_hold_release();
    }
    pthread_mutex_unlock(&s_session_mu);
    if (!active)
        return FL_RESULT_OK;

    /* An action longer than KeyQuorum takes is signed by its first FL_KQ_SUDO_ACTION_MAX bytes. */
    snprintf(act, sizeof act, "%s", action);
    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        now = 0;
    st = now > 0 ? fl_kq_sudo_hold_cosign(cur_name, act, now, nonce, inv_sig, root_sig) : FL_KQ_COSIGN_SIGN;
    if (st == FL_KQ_COSIGN_OK) {
        if (renew) {
            pthread_mutex_lock(&s_session_mu);
            if (s_active_elev == tok && s_login_gen == gen && fl_elevation_refresh(tok, &window) == FL_RESULT_OK)
                fl_kq_sudo_hold_extend(window);
            pthread_mutex_unlock(&s_session_mu);
        }
        fl_hex_encode(nonce, sizeof nonce, nonce_hex);
        nonce_hex[32] = '\0';
        snprintf(detail, sizeof detail, "%s; nonce=%s", renew ? "sudo" : "elevated", nonce_hex);
        session_audit(cur_key, "sudo-action", FL_HERITAGE_ROOT_KEY, detail);
        mem_domain_zero(inv_sig, sizeof inv_sig);
        mem_domain_zero(root_sig, sizeof root_sig);
        return FL_RESULT_OK;
    }
    /* Root's key is not available (owner decision, FL-20 #399): the elevation sudo gave is taken
     * away, and the account goes on with its own key alone. */
    fl_session_drop_elevation();
    session_audit(cur_key, "sudo-deelevated", cur_key,
                  st == FL_KQ_COSIGN_AUTHORITY ? "root-key" : st == FL_KQ_COSIGN_INVOKER ? "invoker-key" : "cosign");
    if (deelevated)
        *deelevated = 1;
    return FL_RESULT_OK;
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
    fl_kq_sudo_hold_release();
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

static void session_lock_db(void) {
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
}

static int session_db_ready_locked(void) {
    return s_db_ready && s_db.loaded;
}

static void session_unlock_db(void) {
    pthread_mutex_unlock(&s_session_mu);
}

int fl_session_users_loaded(void) {
    int loaded;

    session_lock_db();
    loaded = session_db_ready_locked();
    session_unlock_db();
    return loaded;
}

uint32_t fl_session_next_uid(void) {
    uint32_t uid;

    session_lock_db();
    uid = fl_user_db_next_uid(&s_db);
    session_unlock_db();
    return uid;
}

fl_result_t fl_session_add_user(const char *name, const char *password, uint32_t uid, int is_elevated) {
    fl_result_t rc;

    session_lock_db();
    rc = session_db_ready_locked() ? fl_user_db_add_user(&s_db, name, password, uid, is_elevated)
                                   : FL_RESULT_ERR;
    session_unlock_db();
    return rc;
}

fl_result_t fl_session_add_user_under(const char *creator_key, const char *name, const char *password,
                                      uint32_t uid, int is_elevated, int64_t created_at) {
    fl_result_t rc;

    session_lock_db();
    rc = session_db_ready_locked()
             ? fl_user_db_add_user_under(&s_db, creator_key, name, password, uid, is_elevated, created_at)
             : FL_RESULT_ERR;
    session_unlock_db();
    return rc;
}

fl_result_t fl_session_remove_user(const char *name) {
    fl_result_t rc;
    const fl_user_record_t *user;

    session_lock_db();
    if (!session_db_ready_locked()) {
        rc = FL_RESULT_ERR;
    } else {
        /* Same record the session is logged in as, including a different spelling.
         * A switch that committed just before this lock must not leave s_current
         * naming an account that is no longer in the store. */
        user = fl_user_db_find(&s_db, name);
        if (user && user == fl_user_db_find(&s_db, s_current))
            rc = FL_RESULT_ACCES;
        else
            rc = fl_user_db_remove_user(&s_db, name);
    }
    session_unlock_db();
    return rc;
}

/*
 * An ancestor's reset gives the account new keys (its old ones are sealed under a password nobody
 * here has), so a token holding the old keys would no longer match: the account goes back to
 * password sign-in, and its next sign-in has the kernel seal the new keys into a fresh token.
 */
fl_result_t fl_session_set_password(const char *name, const char *password) {
    const fl_user_record_t *u;
    fl_result_t rc;
    int was_kqst = 0;

    session_lock_db();
    if (!session_db_ready_locked()) {
        rc = FL_RESULT_ERR;
    } else {
        u = fl_user_db_find(&s_db, name);
        was_kqst = u && u->auth_mode == FL_USER_AUTH_PASSWORD_KQST;
        rc = fl_user_db_set_password(&s_db, name, password);
        if (rc == FL_RESULT_OK && was_kqst)
            rc = fl_user_db_set_auth_mode(&s_db, name, FL_USER_AUTH_PASSWORD);
    }
    session_unlock_db();
    return rc;
}

/*
 * A token-mode account's token is sealed under its password, so changing the password re-seals the
 * token too, or the next sign-in could not open it. The token is opened with the current password
 * first (it must be present and match its pairing), the new password is saved, then the same keys
 * are sealed under the new password to the same device id and path; if that last step fails, the
 * old password is put back and saved, and the change is refused. Audited as "kqst-resealed" or
 * "kqst-reseal-refused".
 */
fl_result_t fl_session_change_password(const char *name, const char *current, const char *password) {
    const fl_user_record_t *u;
    fl_user_kqst_row_t *row = NULL;
    char key_id[FL_HERITAGE_KEY_ID_MAX], db_path[sizeof s_db.path], path[FL_KQ_TOKEN_PATH_MAX];
    unsigned char sig_pub[32], enc_pub[32];
    uint8_t sig[32], box[32];
    const char *why = NULL;
    fl_result_t rc;
    int kqst = 0;

    fl_session_init();
    session_lock_db();
    if (!session_db_ready_locked()) {
        session_unlock_db();
        return FL_RESULT_ERR;
    }
    u = fl_user_db_find(&s_db, name);
    if (u && u->auth_mode == FL_USER_AUTH_PASSWORD_KQST) {
        kqst = 1;
        snprintf(key_id, sizeof key_id, "%s", u->key_id);
        snprintf(db_path, sizeof db_path, "%s", s_db.path[0] ? s_db.path : FL_USERS_DB_DEFAULT_PATH);
    }
    if (!kqst) {
        rc = fl_user_db_change_password(&s_db, name, current, password);
        session_unlock_db();
        return rc;
    }
    session_unlock_db();

    if (!current || !password)
        return FL_RESULT_INVAL;
    if (!fl_session_verify_account_password(name, current))
        return FL_RESULT_ACCES;
    row = (fl_user_kqst_row_t *)calloc(1, sizeof *row);
    if (!row)
        return FL_RESULT_NOMEM;
    rc = FL_RESULT_ERR;
    if (fl_kq_open_account_keys(name, current, NULL, sig, box) != FL_KQ_ACCOUNT_OK) {
        why = "open";
        goto done;
    }
    session_lock_db();
    u = session_db_ready_locked() ? fl_user_db_find(&s_db, name) : NULL;
    if (!u || strcmp(u->key_id, key_id) != 0 || u->auth_mode != FL_USER_AUTH_PASSWORD_KQST ||
        fl_user_db_kqst_row_get(&s_db, key_id, row) != FL_RESULT_OK || !row->present) {
        why = "changed";
    } else if (row->has_path ? (snprintf(path, sizeof path, "%s", row->kqst_path) >= (int)sizeof path)
                             : (fl_kq_account_token_path(db_path, key_id, path, sizeof path) != 0)) {
        why = "store";
    } else if ((rc = fl_user_db_change_password(&s_db, name, current, password)) != FL_RESULT_OK ||
               fl_user_db_save(&s_db) != FL_RESULT_OK) {
        if (rc == FL_RESULT_OK)
            (void)fl_user_db_change_password(&s_db, name, password, current);
        rc = rc == FL_RESULT_OK ? FL_RESULT_ERR : rc;
        why = "store";
    }
    session_unlock_db();
    if (why)
        goto done;
    if (fl_kq_write_account_token(key_id, password, sig, box, row->device_id, path, sig_pub,
                                  enc_pub) != FL_KQ_ACCOUNT_OK ||
        !asm_mem_ct_equal(sig_pub, row->signing_public, 32) ||
        !asm_mem_ct_equal(enc_pub, row->encryption_public, 32)) {
        /* The token file is replaced only by a rename, so on a failure before it the old token
         * stands; the old password goes back with it. */
        session_lock_db();
        if (fl_user_db_change_password(&s_db, name, password, current) == FL_RESULT_OK)
            (void)fl_user_db_save(&s_db);
        session_unlock_db();
        why = "seal";
        rc = FL_RESULT_ERR;
        goto done;
    }
    rc = FL_RESULT_OK;
done:
    mem_domain_secure_zero(sig, sizeof sig);
    mem_domain_secure_zero(box, sizeof box);
    session_audit(key_id, why ? "kqst-reseal-refused" : "kqst-resealed", key_id, why);
    free(row);
    return rc;
}

fl_result_t fl_session_set_auth_mode(const char *name, int auth_mode) {
    fl_result_t rc;

    session_lock_db();
    rc = session_db_ready_locked() ? fl_user_db_set_auth_mode(&s_db, name, auth_mode) : FL_RESULT_ERR;
    session_unlock_db();
    return rc;
}

fl_result_t fl_session_set_elevated(const char *name, int is_elevated) {
    fl_result_t rc;

    session_lock_db();
    rc = session_db_ready_locked() ? fl_user_db_set_elevated(&s_db, name, is_elevated) : FL_RESULT_ERR;
    session_unlock_db();
    return rc;
}

fl_result_t fl_session_ensure_key(const char *name, const char *password, int *created) {
    fl_result_t rc;

    session_lock_db();
    rc = session_db_ready_locked() ? fl_user_db_ensure_key(&s_db, name, password, created) : FL_RESULT_ERR;
    session_unlock_db();
    return rc;
}

void fl_session_forget_created(const char *name, int created) {
    session_lock_db();
    if (session_db_ready_locked())
        fl_user_db_forget_created(&s_db, name, created);
    session_unlock_db();
}

void fl_session_forget_key(const char *name) {
    session_lock_db();
    if (session_db_ready_locked())
        fl_user_db_forget_key(&s_db, name);
    session_unlock_db();
}

fl_result_t fl_session_reload_users(void) {
    fl_result_t rc;
    char who_key[FL_HERITAGE_KEY_ID_MAX];
    uint64_t failed_gen = 0;
    int offline = 0;

    session_lock_db();
    /* Taken before the load: a failed load clears the records, and the key service needs the key id. */
    {
        const fl_user_record_t *cur = fl_user_db_find(&s_db, s_current);

        who_key[0] = '\0';
        if (cur)
            snprintf(who_key, sizeof who_key, "%s", cur->key_id);
    }
    if (!s_db.path[0])
        rc = FL_RESULT_ERR;
    else
        rc = fl_user_db_load(&s_db, s_db.path);
    if (rc != FL_RESULT_OK) {
        /* The store is not the authority anymore. A token or sudo scope from before
         * this call must not still satisfy fl_session_has_elevation. */
        revoke_bound_elevation_locked();
        s_sudo_scope_depth = 0;
        s_current[0] = '\0';
        s_login_gen++;
        failed_gen = s_login_gen;
        offline = who_key[0] != '\0';
    }
    s_db_ready = (rc == FL_RESULT_OK && s_db.loaded) ? 1 : 0;
    session_unlock_db();
    /* The signed-in account is gone with the session, so its key goes offline (the service wipes it). Only
     * while no later login has begun: that login's generation differs, and its key is not this one's to end. */
    if (offline && s_offline_hook && fl_session_login_generation() == failed_gen)
        s_offline_hook(who_key);
    return rc;
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
    if (!s_db_ready || !s_db.loaded) {
        pthread_mutex_unlock(&s_session_mu);
        return FL_RESULT_ERR;
    }
    rc = fl_user_db_save(&s_db);
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}
