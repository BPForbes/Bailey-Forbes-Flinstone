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
/** Open and wipe both secrets; status only (for refusal checks and tooling). */
int fl_kq_open_account(const char *user, const char *password, const char *kqst_path_or_null);

#ifdef __cplusplus
}
#endif

#endif /* FL_KQ_H */
