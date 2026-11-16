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
 * A switch requirement as KeyQuorum decides it (kq_access_requirement). factor is an FL_KQ_ACCESS_*
 * value; every error is FL_KQ_ACCESS_REFUSED, never INVALID. bridge_index and bridge_uid name the
 * bridge for FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST, else -1 and "".
 */
typedef struct {
    int factor;
    int bridge_index;
    int needs_parent_grant;
    char bridge_uid[FL_KQ_BRIDGE_UID_MAX];
} fl_kq_requirement_t;
struct fl_user_record;
struct fl_user_bridge;
/** Returns what a switch from current_user to target_user demands; sudo non-zero asks for Root's key. */
fl_kq_requirement_t fl_kq_unlock_requirement(const char *current_user, const char *target_user, int sudo);
/**
 * The same rule over records and a live list the caller already holds; takes no session lock and
 * reads no store. Returns REFUSED for a null record or nlive above the store's cap.
 */
fl_kq_requirement_t fl_kq_requirement_from_snapshot(const struct fl_user_record *current,
                                                   const struct fl_user_record *target, int sudo,
                                                   const struct fl_user_bridge *live, size_t nlive);
/**
 * Writes the canonical digest of the bridge facts and the selected member's sealed key as 64
 * lowercase hex chars and a NUL. Returns INVAL when the facts cannot be digested.
 */
fl_result_t fl_kq_bridge_facts_hex(const struct fl_user_bridge *facts, const char *selected_label,
                                   const unsigned char *sealed, size_t sealed_len,
                                   const unsigned char wrap_salt[16], char facts_hex[65]);
/**
 * Proves member_user's membership of bridge uid against the facts the store holds at this call.
 * On FL_KQ_BRIDGE_OK facts_hex (may be NULL) receives their digest; else it is zeroed. Returns
 * FL_KQ_BRIDGE_OK, STALE (destroyed), REFUSED or INVALID. Every secret is wiped before return.
 */
int fl_kq_prove_bridge_member(const char *uid, const char *member_user, const char *member_password,
                              char facts_hex[65]);
/**
 * Open the account's KQST (pairing from the store). Returns KQ_ACCOUNT_*. On OK,
 * signing_secret_out holds the signing secret (caller wipes); the encryption secret is wiped inside.
 */
int fl_kq_open_account_secrets(const char *user, const char *password, const char *kqst_path_or_null,
                               uint8_t signing_secret_out[32]);
/** The same open, keeping both secrets. Caller wipes both on OK; on any other status both are zeroed. */
int fl_kq_open_account_keys(const char *user, const char *password, const char *kqst_path_or_null,
                            uint8_t signing_secret_out[32], uint8_t encryption_secret_out[32]);
/**
 * Opens the token at kqst_path as key_id's slot token against the given device id and public keys
 * instead of the store's pairing. Status only (KQ_ACCOUNT_*); both secrets are wiped.
 */
int fl_kq_check_token(const char *key_id, const char *password, const char *kqst_path,
                      const unsigned char device_id[16], const unsigned char signing_public[32],
                      const unsigned char encryption_public[32]);
/** Open and wipe both secrets; status only (for refusal checks and tooling). */
int fl_kq_open_account(const char *user, const char *password, const char *kqst_path_or_null);

/** Longest path of a token the kernel makes (FL_USER_KQST_PATH_MAX in user_db.h). */
#define FL_KQ_TOKEN_PATH_MAX 512u
/**
 * Writes "<directory of db_path>/kqst/<key_id>.kqst" to out. Returns 0, or -1 when it does not fit
 * or key_id is not a safe file name (letters, digits, '.', '_', '-'; no leading '.', no "..").
 */
int fl_kq_account_token_path(const char *db_path, const char *key_id, char *out, size_t cap);
/**
 * Seals the account's keys with password to device_id into an owner-only KQST at path and writes
 * the two derived public keys on KQ_ACCOUNT_OK. Caller wipes the secrets. Returns
 * KQ_ACCOUNT_INVALID when the driver has no slot_seal or the file cannot be written.
 */
int fl_kq_write_account_token(const char *key_id, const char *password,
                              const uint8_t signing_secret[32], const uint8_t box_secret[32],
                              const unsigned char device_id[16], const char *path,
                              unsigned char signing_public_out[32],
                              unsigned char encryption_public_out[32]);

/** Locks len bytes at p (page-aligned) out of swap, best effort. Returns 1 when locked, else 0. */
int fl_kq_lock_pages(void *p, size_t len);

/** Co-sign results of fl_kq_sudo_hold_open and fl_kq_sudo_hold_cosign. */
#define FL_KQ_COSIGN_OK        0
#define FL_KQ_COSIGN_AUTHORITY 1 /* Root's token did not open with Root's password */
#define FL_KQ_COSIGN_INVOKER   2 /* the invoker's token or key did not open with its password */
#define FL_KQ_COSIGN_SIGN      3 /* no driver op, no nonce, or a signature that did not verify */
/**
 * The sudo window: Root's and the invoker's keys, held until expires_ns (monotonic); each elevated
 * action is signed by both. open with expires_ns > 0 replaces any earlier window; secrets not kept
 * are wiped. cosign returns AUTHORITY when no window is held, it ended or Root's pairing changed.
 */
int  fl_kq_sudo_hold_open(const char *invoker_name, const char *invoker_password, int invoker_kqst,
                          const char *root_name, const char *root_password, const char *action,
                          int64_t expires_ns, uint8_t nonce_out[16], uint8_t invoker_sig_out[64],
                          uint8_t root_sig_out[64]);
int  fl_kq_sudo_hold_cosign(const char *invoker_name, const char *action, int64_t now_ns,
                            uint8_t nonce_out[16], uint8_t invoker_sig_out[64], uint8_t root_sig_out[64]);
void fl_kq_sudo_hold_extend(int64_t expires_ns); /* moves the end */
void fl_kq_sudo_hold_release(void);              /* wipes the window */
int  fl_kq_sudo_hold_expire(int64_t now_ns);     /* wipes it once ended; 1 if it did */
int  fl_kq_sudo_hold_active(int64_t now_ns);     /* 1 while a window is kept */

#ifdef __cplusplus
}
#endif

#endif /* FL_KQ_H */
