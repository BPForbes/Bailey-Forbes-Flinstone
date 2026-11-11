#include "session.h"
#include "elevation.h"
#include "fl/driver/keyquorum.h"
#include "fl/kq.h"
#include "fl/mem_asm.h"
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
static void (*s_switch_pre_lock)(void); /* tests only: before the final lock, mutex not held */
static void (*s_switch_pre_commit)(void); /* tests only: after recheck, both locks held */
static fl_session_audit_fn s_audit_hook;

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

static void session_load_once(void) {
    const char *path = getenv("FL_USERS_DB_PATH");
    fl_result_t rc;

    /* Seeded accounts call the driver before sh.c reaches drivers_init. */
    (void)fl_kq_driver_get_or_register();

    if (!path || !path[0])
        path = FL_SESSION_USERS_PATH;
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
            open_st = fl_kq_open_account_secrets(open_as, password, NULL, signing_secret);
            if (open_st != FL_KQ_ACCOUNT_OK) {
                mem_domain_secure_zero(signing_secret, sizeof signing_secret);
                fprintf(stderr, "login: %s requires its KQST slot token; open refused\n", open_as);
                return FL_RESULT_ACCES;
            }
            pthread_mutex_lock(&s_session_mu);
            if (s_login_gen != attempt_gen) {
                /* A logout, or another login, ran while the token opened: it has the session now. */
                pthread_mutex_unlock(&s_session_mu);
                mem_domain_secure_zero(signing_secret, sizeof signing_secret);
                fprintf(stderr, "login: %s was superseded while its KQST slot token opened\n", open_as);
                return FL_RESULT_ACCES;
            }
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
    fl_kq_requirement_t need;
    uint64_t attempt_gen;
    int holder_auth, holder_kqst, cur_elevated, check_password = 1, proved = 0;
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
             * Owner decision (FL-12): a password-only holder has no token to open, and the default
             * image has no KQST for Root, so enforcing the token would lock every account out of
             * sudo -i and of su to Root or across branches. Such a holder keeps the rule from before
             * KeyQuorum's: its password, and no prompt for sudo -i by an elevated account. Audited,
             * never silent.
             */
            if (!holder_kqst && req->sudo && cur_elevated)
                check_password = 0;
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
            /* The holder's slot token, opened with the holder's password: a password alone never does. */
            if (fl_kq_open_account(holder_name, pw, NULL) != FL_KQ_ACCOUNT_OK)
                why = "kqst";
        } else {
            session_audit(cur_key, req->sudo ? "sudo-kqst-legacy" : "su-kqst-legacy", tgt_key,
                          "password-only account");
        }
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
            cu->is_elevated != cur_elevated) {
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
    if (rc == FL_RESULT_OK)
        s_active_elev = *out;
    return rc;
}

fl_result_t fl_session_sudo_authenticate(const char *password) {
    const fl_user_record_t *root, *cu;
    char cur_name[FL_USER_NAME_MAX], cur_key[FL_HERITAGE_KEY_ID_MAX], root_name[FL_USER_NAME_MAX];
    fl_kq_requirement_t need;
    fl_elevation_token_t tok = FL_ELEVATION_TOKEN_NONE;
    uint64_t attempt_gen;
    int root_kqst;
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
    attempt_gen = s_login_gen;
    pthread_mutex_unlock(&s_session_mu);

    need = fl_kq_unlock_requirement(cur_name, cur_name, 1);
    if (need.needs_parent_grant)
        why = "parent-grant";
    else if (need.factor != FL_KQ_ACCESS_KQST_PASSWORD)
        why = "policy";
    else if (root_kqst) {
        if (!fl_session_verify_account_password(root_name, password))
            why = "password";
        else if (fl_kq_open_account(root_name, password, NULL) != FL_KQ_ACCOUNT_OK)
            why = "kqst";
    } else if (!fl_session_verify_account_password(cur_name, password)) {
        why = "password"; /* password-only Root: the caller's own password, as before (owner decision) */
    } else {
        session_audit(cur_key, "sudo-kqst-legacy", FL_HERITAGE_ROOT_KEY, "password-only account");
    }
    if (why) {
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, why);
        return FL_RESULT_ACCES;
    }
    /* The password check ran without the session lock. A login or logout in that
     * window changes the generation. set_auth_mode and set_password do not, so
     * the mode and the password that were accepted are checked again here.
     * The elevation token is taken for the account that was checked, or not at all. */
    if (s_switch_pre_lock)
        s_switch_pre_lock();
    pthread_mutex_lock(&s_session_mu);
    if (s_login_gen != attempt_gen || strcmp(s_current, cur_name) != 0) {
        pthread_mutex_unlock(&s_session_mu);
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, "superseded");
        return FL_RESULT_BUSY;
    }
    root = fl_user_db_find_by_key(&s_db, FL_HERITAGE_ROOT_KEY);
    cu = fl_user_db_find(&s_db, cur_name);
    if (!s_db_ready || !s_db.loaded || !root || !cu ||
        (root->auth_mode == FL_USER_AUTH_PASSWORD_KQST) != root_kqst) {
        pthread_mutex_unlock(&s_session_mu);
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, "mode");
        return FL_RESULT_ACCES;
    }
    if (!session_verify_password_locked(root_kqst ? root_name : cur_name, password)) {
        pthread_mutex_unlock(&s_session_mu);
        session_audit(cur_key, "sudo-refused", FL_HERITAGE_ROOT_KEY, "password");
        return FL_RESULT_ACCES;
    }
    rc = grant_elevation_locked("sudo authentication", &tok);
    pthread_mutex_unlock(&s_session_mu);
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

fl_result_t fl_session_set_password(const char *name, const char *password) {
    fl_result_t rc;

    session_lock_db();
    rc = session_db_ready_locked() ? fl_user_db_set_password(&s_db, name, password) : FL_RESULT_ERR;
    session_unlock_db();
    return rc;
}

fl_result_t fl_session_change_password(const char *name, const char *current, const char *password) {
    fl_result_t rc;

    session_lock_db();
    rc = session_db_ready_locked() ? fl_user_db_change_password(&s_db, name, current, password)
                                   : FL_RESULT_ERR;
    session_unlock_db();
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

    session_lock_db();
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
    }
    s_db_ready = (rc == FL_RESULT_OK && s_db.loaded) ? 1 : 0;
    session_unlock_db();
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
