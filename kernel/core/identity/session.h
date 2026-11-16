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
/**
 * Sets the session's account with no check at all: for restoring the identity a command already held
 * (the end of su -c) and for tests. A new identity is taken only through fl_session_switch_user.
 */
fl_result_t fl_session_set_user(const char *name);
/**
 * The restore at the end of su -c: takes name back only while login_generation (read before the
 * switch, fl_session_login_generation) is still the current login. A login or logout in between
 * leaves the session as it is: BUSY, audited as "su-restore-refused". INVAL for generation 0;
 * otherwise as fl_session_set_user.
 */
fl_result_t fl_session_restore_user(const char *name, uint64_t login_generation);
/** 1 when password is the named account's password (checked under the session lock), else 0. */
int         fl_session_verify_account_password(const char *name, const char *password);

/**
 * An identity switch (su, sudo -i). The commands collect what KeyQuorum's rule asks for; the session
 * asks fl_kq_unlock_requirement again itself and decides, so no caller can choose a weaker factor.
 *   NONE                 nothing more (the same account, without sudo).
 *   PASSWORD             password is the target's.
 *   KQST_PASSWORD        password is the token holder's (Root's for sudo, else the target's) and opens
 *                        its slot token. For sudo -i, a password-only Root is refused ("no-root-kqst"),
 *                        whoever asks (owner decision, FL-12 #403). For su, a password-only (legacy)
 *                        target keeps the rule from before KeyQuorum's (its password), audited as
 *                        "su-kqst-legacy".
 *   PASSWORD_BRIDGE_KQST the target's password, plus bridge_member opening their token with
 *                        bridge_member_password and proving membership (fl_kq_prove_bridge_member).
 *   REFUSED              refused. A switch that needs a parent's grant is refused too: there is no way
 *                        to collect one.
 * The password is checked outside the final lock. That lock is taken before a BEGIN IMMEDIATE
 * reservation of the account database, and both are released on every path. Under them the factor
 * is recomputed from the reserved snapshot, and a proof is accepted only when the canonical digest
 * of the current facts matches the digest the proof returned. A proof that no longer matches is
 * "bridge-changed". A factor, holder mode, password record, or caller elevation that changed is
 * "changed". A login or logout in between is BUSY "superseded". A writer already holding the
 * database is BUSY "busy".
 * Every refusal is audited through the audit hook ("su-refused" / "sudo-refused", the two key ids
 * and a reason word; never a password, key or token byte) and nothing names the reason on the
 * terminal. FL_RESULT_OK, ACCES (refused), BUSY (a login or logout ran meanwhile, or the writer
 * slot is taken), NOENT (no such account) or INVAL.
 */
typedef struct fl_session_switch_request {
    const char *target;
    int         sudo;
    const char *password;
    const char *bridge_member;
    const char *bridge_member_password;
    /** sudo -i only: the invoking account's own password (its half of the co-authorisation; it
     *  also opens the invoker's token when the invoker is a KQST-mode account). Unused when the
     *  invoker is Root. */
    const char *invoker_password;
} fl_session_switch_request_t;
fl_result_t fl_session_switch_user(const fl_session_switch_request_t *req);

/**
 * sudo without a switch (an elevation token), co-authorised by two identities. KeyQuorum asks for
 * Root's key, so root_password must be Root's and open Root's token. A password-only Root has no
 * token: sudo is refused ("no-root-kqst") before any password is checked, and there is no fallback
 * to the caller's own password (owner decision, FL-12 #403). Beside Root's factor, the invoking
 * account gives its own (owner decision on PR 416): invoker_password must be its password and, for
 * a KQST-mode invoker, open its token ("invoker-password" / "invoker-kqst" when not). Root invoking
 * for itself gives Root's factor once. OK also binds the elevation token to that same login: a
 * login or logout during the check returns BUSY and grants nothing; a change to either account's
 * mode ("mode") or password in that window is refused. A grant is audited as "sudo-coauth" naming
 * the invoker's factor. Any other refusal is ACCES.
 */
fl_result_t fl_session_sudo_authenticate(const char *root_password, const char *invoker_password);
/**
 * Sign one elevated action (owner decision, FL-20 #399). While the sudo window is open the action is
 * signed by Root's key and the account's key together (audited "sudo-action" with the nonce); renew
 * non-zero (a sudo) also starts the window's clock again. When the window has ended it does nothing:
 * the account acts with its own key. When Root's key is not available the elevation is taken away,
 * audited "sudo-deelevated", and *deelevated (may be NULL) is set; the account goes on unelevated.
 * Root acts with its own key and has no window. Called without the session lock.
 */
fl_result_t fl_session_elevated_action(const char *action, int renew, int *deelevated);
/**
 * Pair the logged-in account with its KeyQuorum slot token (KQST) and switch it to KQST login.
 * password is the account's password, which must also open the token; the token must match
 * device_id, signing_public and encryption_public (what keyquorum-device provision printed for a
 * slot labelled with the account's key id). An account that already has a token must open that one
 * too: a password alone never replaces a token. kqst_path is stored as given, so the token stays on
 * its device. OK (generation_out, may be NULL, is the pairing's generation), ACCES (refused:
 * "password", "old-kqst", "kqst", "changed"), BUSY (a login or logout ran meanwhile), ERR (the store
 * could not be written; nothing changed), NOENT (nobody is logged in) or INVAL. Audited as
 * "kqst-enrol" or "kqst-enrol-refused".
 */
fl_result_t fl_session_enroll_kqst(const char *password, const char *kqst_path,
                                   const unsigned char device_id[16],
                                   const unsigned char signing_public[32],
                                   const unsigned char encryption_public[32], int64_t minted_at,
                                   int64_t *generation_out);
/**
 * Give the account name its slot token from the kernel itself: its own signing and box keys, opened
 * with password, sealed with password to a fresh random device id into a KQST beside the account
 * store, then paired and switched to token sign-in (as fl_session_enroll_kqst does, same rollback).
 * The account keeps its keys. OK (generation_out, may be NULL), ACCES ("exists": it already has a
 * token; "password"; "changed"), NOSYS (no driver can seal one: the freestanding image or a test
 * stub), ERR ("no-key", "key", "seal", "store"), NOENT or INVAL. Audited as "kqst-made" or
 * "kqst-make-refused". Called at the account's sign-in, by useradd, and by kqst enroll.
 */
fl_result_t fl_session_make_kqst(const char *name, const char *password, int64_t minted_at,
                                 int64_t *generation_out);
/** 1 when Root is a KQST-mode account, so sudo can ask for Root's password and token; 0 when Root
 *  is password-only, and sudo and sudo -i are refused. */
int         fl_session_sudo_needs_root(void);

/** Where the session reports identity events (the shell installs fl_audit_identity_event). */
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
 * The box (X25519) secret a KQST login opened next to the signing secret (FL-15). Runs right after online_opened
 * succeeded, for the same login generation; the service keeps it for the life of the session and wipes it at
 * logout. A refusal refuses the login like online_opened does. Optional: NULL keeps the old behaviour (the
 * box secret is wiped as soon as the token has opened).
 */
typedef fl_result_t (*fl_session_online_box_fn)(const fl_user_record_t *account, const uint8_t box_secret[32],
                                                uint64_t login_generation);
void fl_session_set_login_box_hook(fl_session_online_box_fn box);
void fl_session_set_login_hooks(fl_session_online_fn online, fl_session_offline_fn offline,
                                fl_session_online_opened_fn opened);
/** Tests only: runs after the session lock is released and before the KQST open. */
void fl_session_test_set_kqst_pre_open(void (*fn)(void));
/**
 * Tests only: runs immediately before the final lock of a switch, after the factor was satisfied
 * and before the writer reservation. The session lock is not held.
 */
void fl_session_test_set_switch_pre_lock(void (*fn)(void));
/**
 * Tests only: runs after the final recheck has succeeded and before the identity commit, while
 * the session lock and the writer reservation are both held. Must not call any fl_session_* that
 * takes the session lock.
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

/** 1 when the session account store is loaded. */
int         fl_session_users_loaded(void);
/** Next free uid in the loaded session store. */
uint32_t    fl_session_next_uid(void);
/**
 * The user_db mutators and the disk reload, run with the session mutex held so a switch that
 * already holds a user-record pointer cannot race them. The mutex is taken before any SQLite
 * those calls open. A store that is not loaded returns FL_RESULT_ERR (reload returns the load
 * result and clears the session when the file is bad).
 */
fl_result_t fl_session_add_user(const char *name, const char *password, uint32_t uid, int is_elevated);
fl_result_t fl_session_add_user_under(const char *creator_key, const char *name, const char *password,
                                      uint32_t uid, int is_elevated, int64_t created_at);
fl_result_t fl_session_remove_user(const char *name);
fl_result_t fl_session_set_password(const char *name, const char *password);
fl_result_t fl_session_change_password(const char *name, const char *current, const char *password);
fl_result_t fl_session_set_auth_mode(const char *name, int auth_mode);
fl_result_t fl_session_set_elevated(const char *name, int is_elevated);
fl_result_t fl_session_ensure_key(const char *name, const char *password, int *created);
void        fl_session_forget_created(const char *name, int created);
void        fl_session_forget_key(const char *name);
fl_result_t fl_session_reload_users(void);

#endif /* FL_SESSION_H */
