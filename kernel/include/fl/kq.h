#ifndef FL_KQ_H
#define FL_KQ_H

/*
 * The monitor's entry points. Every body calls fl_kq_driver_get() and the existing fl_kq_cp_*
 * checkpoints; nothing here talks to Rust directly, so no behaviour is decided twice.
 */

#include "contract_result.h"
#include "fl/driver/keyquorum.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Wire-visible: append only, never renumber. */
typedef enum {
    FL_KQ_OP_CREATE = 1,
    FL_KQ_OP_WRITE,
    FL_KQ_OP_MKDIR,
    FL_KQ_OP_DELETE,
    FL_KQ_OP_MOVE,
    FL_KQ_OP_RECEIVE
} fl_kq_op_t;

fl_result_t fl_kq_pre_mutation(const char *path);
fl_result_t fl_kq_after_mutation(const char *path, fl_kq_op_t op); /* tier 1: sign as the current account */
fl_result_t fl_kq_gate_receive(const kq_file_offer_t *o, const uint8_t *data, size_t len,
                               uint16_t receiver);
fl_result_t fl_kq_after_receive(const char *landed_path, const kq_file_offer_t *o);
void fl_kq_report_refusal(const kq_file_offer_t *o, fl_result_t why); /* writes the KQER sidecar */
fl_result_t fl_kq_block_written(uint32_t lba);

/** A KeyQuorum private bridge uid and its NUL (FL_USER_BRIDGE_UID_MAX in user_db.h). */
#define FL_KQ_BRIDGE_UID_MAX 65u

/**
 * The rule lives in KeyQuorum (kq_access_requirement); this only asks it. factor is an
 * FL_KQ_ACCESS_* factor and never INVALID: any error (no driver, unknown account, a store that
 * cannot be read) is FL_KQ_ACCESS_REFUSED. For FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST, bridge_index is
 * the bridge's place in the live list the store held at the call and bridge_uid names it; else -1
 * and "". needs_parent_grant is KeyQuorum's flag, unchanged.
 */
typedef struct {
    int factor;
    int bridge_index;
    int needs_parent_grant;
    char bridge_uid[FL_KQ_BRIDGE_UID_MAX];
} fl_kq_requirement_t;
struct fl_user_record;
struct fl_user_bridge;
/**
 * What a switch from current_user to target_user (account names) demands; sudo non-zero asks for
 * Root's key. The accounts are mapped to their heritage key ids and the live bridges are those the
 * account store holds as live now (user_db.h, fl_user_db_bridges_live). parent_approval is 0: the
 * store keeps no unlock_approval policy.
 */
fl_kq_requirement_t fl_kq_unlock_requirement(const char *current_user, const char *target_user, int sudo);
/**
 * The same rule for accounts and a live list the caller already holds. Does not lock the session
 * and does not read the store. nlive above the store's cap, or a null record, is REFUSED.
 */
fl_kq_requirement_t fl_kq_requirement_from_snapshot(const struct fl_user_record *current,
                                                   const struct fl_user_record *target, int sudo,
                                                   const struct fl_user_bridge *live, size_t nlive);
/**
 * Canonical digest of the bridge facts and the selected member's sealed key: 64 lowercase hex
 * chars and a NUL. Lengths are big-endian; the raw struct is not hashed. INVAL when the facts
 * cannot be digested. The canonical buffer is wiped before return.
 */
fl_result_t fl_kq_bridge_facts_hex(const struct fl_user_bridge *facts, const char *selected_label,
                                   const unsigned char *sealed, size_t sealed_len,
                                   const unsigned char wrap_salt[16], char facts_hex[65]);
/**
 * A bridge member proves membership of bridge uid at a switch: member_user's slot token is opened
 * with member_password (pairing from the store), its sealed bridge key and wrap salt come from the
 * store, a fresh random challenge is signed (bridge_prove) and the artifact is verified
 * (bridge_verify) against the bridge's facts read from the store at this call, never the caller's.
 * The facts and the sealed key are one snapshot, copied before the token is opened. On
 * FL_KQ_BRIDGE_OK, facts_hex (when non-NULL) receives the canonical digest of those facts; any
 * other status zeroes it. A null facts_hex keeps the status-only call. Returns FL_KQ_BRIDGE_OK,
 * or FL_KQ_BRIDGE_STALE (destroyed), FL_KQ_BRIDGE_REFUSED (not a member, no sealed key held, the
 * token did not open, the proof did not verify) or FL_KQ_BRIDGE_INVALID. Every secret is wiped
 * before it returns.
 */
int fl_kq_prove_bridge_member(const char *uid, const char *member_user, const char *member_password,
                              char facts_hex[65]);
/**
 * Open the account's KQST (pairing from the store). Returns KQ_ACCOUNT_*. On OK,
 * signing_secret_out holds the signing secret (caller wipes); the encryption secret is wiped inside.
 */
int fl_kq_open_account_secrets(const char *user, const char *password, const char *kqst_path_or_null,
                               uint8_t signing_secret_out[32]);
/**
 * The same open, keeping both secrets: on OK signing_secret_out and encryption_secret_out hold them
 * (the caller wipes both); on any other status both are zeroed.
 */
int fl_kq_open_account_keys(const char *user, const char *password, const char *kqst_path_or_null,
                            uint8_t signing_secret_out[32], uint8_t encryption_secret_out[32]);
/**
 * Open the token file at kqst_path as key_id's slot token, with the device id and public keys
 * given here instead of the store's pairing (enrolment checks a token before the store holds it).
 * Status only: both secrets are wiped. KQ_ACCOUNT_OK when the token opens with password and matches
 * every fact; any other KQ_ACCOUNT_* status otherwise.
 */
int fl_kq_check_token(const char *key_id, const char *password, const char *kqst_path,
                      const unsigned char device_id[16], const unsigned char signing_public[32],
                      const unsigned char encryption_public[32]);
/** Open and wipe both secrets; status only (for refusal checks and tooling). */
int fl_kq_open_account(const char *user, const char *password, const char *kqst_path_or_null);

/** Longest path of a token the kernel makes (the account store's FL_USER_KQST_PATH_MAX). */
#define FL_KQ_TOKEN_PATH_MAX 512u
/**
 * Where the kernel keeps key_id's token: "<directory of db_path>/kqst/<key_id>.kqst". 0, or -1 when
 * it does not fit in cap or key_id is not a safe file name (letters, digits, '.', '_' and '-' only,
 * not starting with '.', no ".."). The token is written only into a "kqst" directory this process
 * owns that no one else can write to, and never through a symlink.
 */
int fl_kq_account_token_path(const char *db_path, const char *key_id, char *out, size_t cap);
/**
 * Seal an account's own keys (signing_secret, box_secret) with password to device_id into a KQST
 * and write it to path (owner-only, through a randomly named ".new" sibling and a rename, so the
 * old token stays whole if this fails). On KQ_ACCOUNT_OK the two public keys derived from the
 * secrets are written for the store's pairing. The caller wipes the secrets. KQ_ACCOUNT_INVALID when the driver has no
 * slot_seal (the freestanding image) or the file cannot be written.
 */
int fl_kq_write_account_token(const char *key_id, const char *password,
                              const uint8_t signing_secret[32], const uint8_t box_secret[32],
                              const unsigned char device_id[16], const char *path,
                              unsigned char signing_public_out[32],
                              unsigned char encryption_public_out[32]);

/**
 * Keeps len bytes at p (page-aligned) out of swap, and on Linux out of core dumps. Best effort:
 * returns 1 when the pages are locked, 0 when the host refused (RLIMIT_MEMLOCK) or cannot.
 */
int fl_kq_lock_pages(void *p, size_t len);

/** Co-sign results (fl_kq_sudo_hold_open, fl_kq_sudo_hold_cosign). */
#define FL_KQ_COSIGN_OK        0
#define FL_KQ_COSIGN_AUTHORITY 1 /* Root's token did not open with Root's password */
#define FL_KQ_COSIGN_INVOKER   2 /* the invoker's token or key did not open with its password */
#define FL_KQ_COSIGN_SIGN      3 /* no driver op, no nonce, or a signature that did not verify */
/**
 * The sudo window: Root's key and the invoker's key, kept only until the window ends and never
 * taken from a stored credential; each elevated action in it is signed by both.
 *
 * fl_kq_sudo_hold_open opens Root's token with root_password and the invoker's key with
 * invoker_password (its token when invoker_kqst, else its sealed signing key); both sign action
 * over a fresh nonce and both signatures must verify under the account store's public keys. On
 * FL_KQ_COSIGN_OK the outputs hold the audit record, and with expires_ns > 0 (monotonic) the two
 * secrets are kept until then, replacing any earlier window; otherwise the signatures are zeroed.
 * Every secret not kept is wiped before it returns.
 * fl_kq_sudo_hold_cosign signs action with both kept keys and verifies under the store's public
 * keys now. FL_KQ_COSIGN_AUTHORITY when no window is kept for invoker_name, it has ended (it is
 * wiped) or Root's token pairing changed; FL_KQ_COSIGN_INVOKER when the invoker's did. Called
 * without the session lock.
 * extend moves the end; release wipes the window; expire wipes it once ended and answers 1 if it
 * did; active answers 1 while it is kept.
 */
int  fl_kq_sudo_hold_open(const char *invoker_name, const char *invoker_password, int invoker_kqst,
                          const char *root_name, const char *root_password, const char *action,
                          int64_t expires_ns, uint8_t nonce_out[16], uint8_t invoker_sig_out[64],
                          uint8_t root_sig_out[64]);
int  fl_kq_sudo_hold_cosign(const char *invoker_name, const char *action, int64_t now_ns,
                            uint8_t nonce_out[16], uint8_t invoker_sig_out[64], uint8_t root_sig_out[64]);
void fl_kq_sudo_hold_extend(int64_t expires_ns);
void fl_kq_sudo_hold_release(void);
int  fl_kq_sudo_hold_expire(int64_t now_ns);
int  fl_kq_sudo_hold_active(int64_t now_ns);

#ifdef __cplusplus
}
#endif

#endif /* FL_KQ_H */
