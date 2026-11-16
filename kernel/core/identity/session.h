#ifndef FL_SESSION_H
#define FL_SESSION_H

#include "contract_identity.h"
#include "user_db.h"

#define FL_SESSION_USERS_PATH FL_USERS_DB_DEFAULT_PATH

void        fl_session_init(void);
const char *fl_session_current_user(void);

const char *fl_session_current_key_id(void);

int         fl_session_is_elevated_account(void);

int         fl_session_has_elevation(void);

/** VM jail escape: elevated account or sudo command scope (not token-only). */
int         fl_session_jail_privileged(void);

int         fl_session_in_sudo_scope(void);
void        fl_session_begin_sudo_scope(void);
void        fl_session_end_sudo_scope(void);

fl_result_t fl_session_login(const char *name, const char *password);
/** Sets the session's account with no check; for restores and tests only. */
fl_result_t fl_session_set_user(const char *name);
/** The account and login generation su -c restores at its end. */
typedef struct fl_session_restore_point {
    char     user[FL_USER_NAME_MAX];
    uint64_t login_generation;
} fl_session_restore_point_t;
/** Fills out with the current account and login generation under one hold of the session lock. */
fl_result_t fl_session_capture_restore_point(fl_session_restore_point_t *out);
/**
 * Restores name only while login_generation is still the current login.
 * Returns BUSY when a login or logout ran in between, INVAL for generation 0, else as fl_session_set_user.
 */
fl_result_t fl_session_restore_user(const char *name, uint64_t login_generation);
/** Returns 1 when password is the named account's password, else 0. */
int         fl_session_verify_account_password(const char *name, const char *password);

/**
 * An identity switch request (su, sudo -i). The session recomputes the factor from
 * fl_kq_unlock_requirement itself, so no caller can choose a weaker one; a switch needing a parent's
 * grant is refused. Refusals are audited without any secret byte.
 */
typedef struct fl_session_switch_request {
    const char *target;
    int         sudo;
    const char *password;               /* the target's, or the token holder's for a KQST factor */
    const char *bridge_member;          /* PASSWORD_BRIDGE_KQST only */
    const char *bridge_member_password;
    const char *invoker_password;       /* sudo -i only; unused when the invoker is Root */
} fl_session_switch_request_t;
/**
 * Performs the switch. Returns OK, ACCES (refused), BUSY (a login or logout ran meanwhile, or the
 * account database writer slot is taken), NOENT (no such account) or INVAL.
 */
fl_result_t fl_session_switch_user(const fl_session_switch_request_t *req);

/**
 * Grants an elevation token co-authorised by Root's factor and the invoker's own.
 * Returns OK (token bound to this login), BUSY when a login or logout ran during the check, else
 * ACCES. A password-only Root is always refused; there is no fallback to the caller's password.
 */
fl_result_t fl_session_sudo_authenticate(const char *root_password, const char *invoker_password);
/**
 * Signs one elevated action with Root's key and the account's key while the sudo window is open;
 * renew non-zero restarts the window. When Root's key is unavailable the elevation is removed and
 * *deelevated (may be NULL) is set. Call without the session lock.
 */
fl_result_t fl_session_elevated_action(const char *action, int renew, int *deelevated);
/**
 * Pairs the logged-in account with its slot token (KQST). An existing token must also open: a
 * password alone never replaces a token. Returns OK, ACCES (refused), BUSY (login or logout
 * meanwhile), ERR (store not written; nothing changed), NOENT (nobody logged in) or INVAL.
 */
fl_result_t fl_session_enroll_kqst(const char *password, const char *kqst_path,
                                   const unsigned char device_id[16],
                                   const unsigned char signing_public[32],
                                   const unsigned char encryption_public[32], int64_t minted_at,
                                   int64_t *generation_out);
/**
 * Seals the account's own keys into a new KQST beside the account store and pairs it. Returns OK,
 * ACCES (token exists, wrong password or changed), NOSYS (no sealing driver), ERR, NOENT or INVAL.
 */
fl_result_t fl_session_make_kqst(const char *name, const char *password, int64_t minted_at,
                                 int64_t *generation_out);
/** Returns 1 when Root is a KQST-mode account; 0 means sudo and sudo -i are refused. */
int         fl_session_sudo_needs_root(void);

/** Receiver of session identity events. */
typedef void (*fl_session_audit_fn)(const char *actor_key, const char *event, const char *subject_key,
                                    const char *detail);
void fl_session_set_audit_hook(fl_session_audit_fn fn);

int         fl_session_verify_password(const char *password);

fl_result_t fl_session_grant_elevation(const char *reason, fl_elevation_token_t *out);
void        fl_session_bind_elevation(fl_elevation_token_t token);
void        fl_session_clear_elevation(void);

void        fl_session_drop_elevation(void);

fl_result_t fl_session_logout(void);

/**
 * Hooks a service below the session installs (the KeyQuorum key service, kernel/core/vfs/kq_service.c):
 * online runs after a successful fl_session_login with the password just verified (the session lock
 * is not held, so a slow key derivation does not stall the session); online_opened publishes an
 * already-opened signing secret for PASSWORD_KQST logins; offline runs after fl_session_logout with
 * the account that signed out. Password online cannot refuse a login; KQST login refuses when open
 * or online_opened fails.
 */
typedef fl_result_t (*fl_session_online_fn)(const fl_user_record_t *account, const char *password,
                                           uint64_t login_generation);
typedef fl_result_t (*fl_session_online_opened_fn)(const fl_user_record_t *account,
                                                   const uint8_t secret[32], uint64_t login_generation);
typedef void (*fl_session_offline_fn)(const char *account);
/**
 * Receives the box (X25519) secret a KQST login opened, right after online_opened for the same
 * login generation. A refusal refuses the login. With a NULL hook the session wipes the secret.
 */
typedef fl_result_t (*fl_session_online_box_fn)(const fl_user_record_t *account, const uint8_t box_secret[32],
                                                uint64_t login_generation);
void fl_session_set_login_box_hook(fl_session_online_box_fn box);
void fl_session_set_login_hooks(fl_session_online_fn online, fl_session_offline_fn offline,
                                fl_session_online_opened_fn opened);
/** Tests only: runs after the session lock is released and before the KQST open. */
void fl_session_test_set_kqst_pre_open(void (*fn)(void));
/** Tests only: runs just before the final lock of a switch; the session lock is not held. */
void fl_session_test_set_switch_pre_lock(void (*fn)(void));
/**
 * Tests only: runs before the identity commit with the session lock and the writer reservation held.
 * Must not call any fl_session_* that takes the session lock.
 */
void fl_session_test_set_switch_pre_commit(void (*fn)(void));

/**
 * The online hook gets a copy of the account's record taken under the session lock and the generation
 * of the login that made it. Every login and every logout starts a new generation (never 0), so a key
 * derived for one login can be published only while that login is still the current one:
 * **fl_session_publish_if_current** runs publish under the session lock, so a logout either happens
 * before it (BUSY, nothing is published) or after it (the offline hook then wipes what it published).
 * Generation 0 publishes unconditionally. Publish must not call back into the session.
 */
typedef fl_result_t (*fl_session_publish_fn)(void *ctx);
fl_result_t fl_session_publish_if_current(uint64_t login_generation, fl_session_publish_fn publish, void *ctx);
uint64_t fl_session_login_generation(void);
/** A copy of an account's record taken under the session lock. NOENT for an unknown account. */
fl_result_t fl_session_snapshot_account(const char *name, fl_user_record_t *out);

fl_user_db_t *fl_session_user_db_mut(void);
const fl_user_db_t *fl_session_user_db(void);
fl_result_t fl_session_save_users(void);

/** Returns 1 when the session account store is loaded. */
int         fl_session_users_loaded(void);
/** Returns the next free uid in the loaded session store. */
uint32_t    fl_session_next_uid(void);
/**
 * The user_db mutators and the disk reload, run with the session mutex held (taken before any SQLite
 * open). Return FL_RESULT_ERR when the store is not loaded.
 */
fl_result_t fl_session_add_user(const char *name, const char *password, uint32_t uid, int is_elevated);
fl_result_t fl_session_add_user_under(const char *creator_key, const char *name, const char *password,
                                      uint32_t uid, int is_elevated, int64_t created_at);
fl_result_t fl_session_remove_user(const char *name);
fl_result_t fl_session_set_password(const char *name, const char *password);
fl_result_t fl_session_change_password(const char *name, const char *current, const char *password);
fl_result_t fl_session_set_auth_mode(const char *name, int auth_mode);
fl_result_t fl_session_ensure_key(const char *name, const char *password, int *created);
void        fl_session_forget_created(const char *name, int created);
void        fl_session_forget_key(const char *name);
fl_result_t fl_session_reload_users(void);

#endif /* FL_SESSION_H */
