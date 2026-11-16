#ifndef FL_SESSION_H
#define FL_SESSION_H

#include "contract_identity.h"
#include "user_db.h"
/* Login, identity switches, sudo elevation and the KeyQuorum login hooks. */

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
fl_result_t fl_session_set_user(const char *name);
typedef struct fl_session_restore_point {
    char     user[FL_USER_NAME_MAX];
    uint64_t login_generation;
} fl_session_restore_point_t;
fl_result_t fl_session_capture_restore_point(fl_session_restore_point_t *out);
/* BUSY when a login or logout intervened (generation mismatch). */
fl_result_t fl_session_restore_user(const char *name, uint64_t login_generation);
int         fl_session_verify_account_password(const char *name, const char *password);

typedef struct fl_session_switch_request {
    const char *target;
    int         sudo;
    const char *password;
    const char *bridge_member;
    const char *bridge_member_password;
    const char *invoker_password;
} fl_session_switch_request_t;
/* Refusals: ACCES, BUSY (superseded or writer busy), NOENT, INVAL. */
fl_result_t fl_session_switch_user(const fl_session_switch_request_t *req);

/* A password-only Root is refused; no fallback to the caller's password. */
fl_result_t fl_session_sudo_authenticate(const char *root_password, const char *invoker_password);
/* Call without the session lock. */
fl_result_t fl_session_elevated_action(const char *action, int renew, int *deelevated);
/* ACCES refused; BUSY when a login or logout intervened; ERR changes nothing. */
fl_result_t fl_session_enroll_kqst(const char *password, const char *kqst_path,
                                   const unsigned char device_id[16],
                                   const unsigned char signing_public[32],
                                   const unsigned char encryption_public[32], int64_t minted_at,
                                   int64_t *generation_out);
/* NOSYS when no driver can seal a token. */
fl_result_t fl_session_make_kqst(const char *name, const char *password, int64_t minted_at,
                                 int64_t *generation_out);
int         fl_session_sudo_needs_root(void);

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
typedef fl_result_t (*fl_session_online_box_fn)(const fl_user_record_t *account, const uint8_t box_secret[32],
                                                uint64_t login_generation);
void fl_session_set_login_box_hook(fl_session_online_box_fn box);
void fl_session_set_login_hooks(fl_session_online_fn online, fl_session_offline_fn offline,
                                fl_session_online_opened_fn opened);
/** Tests only: runs after the session lock is released and before the KQST open. */
void fl_session_test_set_kqst_pre_open(void (*fn)(void));
void fl_session_test_set_switch_pre_lock(void (*fn)(void));
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

int         fl_session_users_loaded(void);
uint32_t    fl_session_next_uid(void);
/* Store mutators run under the session mutex. */
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
