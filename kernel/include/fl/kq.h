#ifndef FL_KQ_H
#define FL_KQ_H

/* Every body calls fl_kq_driver_get() and the fl_kq_cp_* checkpoints; no policy here. */

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

#define FL_KQ_BRIDGE_UID_MAX 65u

typedef struct {
    int factor;
    int bridge_index;
    int needs_parent_grant;
    char bridge_uid[FL_KQ_BRIDGE_UID_MAX];
} fl_kq_requirement_t;
struct fl_user_record;
struct fl_user_bridge;
fl_kq_requirement_t fl_kq_unlock_requirement(const char *current_user, const char *target_user, int sudo);
fl_kq_requirement_t fl_kq_requirement_from_snapshot(const struct fl_user_record *current,
                                                   const struct fl_user_record *target, int sudo,
                                                   const struct fl_user_bridge *live, size_t nlive);
fl_result_t fl_kq_bridge_facts_hex(const struct fl_user_bridge *facts, const char *selected_label,
                                   const unsigned char *sealed, size_t sealed_len,
                                   const unsigned char wrap_salt[16], char facts_hex[65]);
int fl_kq_prove_bridge_member(const char *uid, const char *member_user, const char *member_password,
                              char facts_hex[65]);
int fl_kq_open_account_secrets(const char *user, const char *password, const char *kqst_path_or_null,
                               uint8_t signing_secret_out[32]);
/* Caller wipes both on OK; both are zeroed otherwise. */
int fl_kq_open_account_keys(const char *user, const char *password, const char *kqst_path_or_null,
                            uint8_t signing_secret_out[32], uint8_t encryption_secret_out[32]);
int fl_kq_check_token(const char *key_id, const char *password, const char *kqst_path,
                      const unsigned char device_id[16], const unsigned char signing_public[32],
                      const unsigned char encryption_public[32]);
int fl_kq_open_account(const char *user, const char *password, const char *kqst_path_or_null);

#define FL_KQ_TOKEN_PATH_MAX 512u
int fl_kq_account_token_path(const char *db_path, const char *key_id, char *out, size_t cap);
int fl_kq_write_account_token(const char *key_id, const char *password,
                              const uint8_t signing_secret[32], const uint8_t box_secret[32],
                              const unsigned char device_id[16], const char *path,
                              unsigned char signing_public_out[32],
                              unsigned char encryption_public_out[32]);

int fl_kq_lock_pages(void *p, size_t len);

#define FL_KQ_COSIGN_OK        0
#define FL_KQ_COSIGN_AUTHORITY 1
#define FL_KQ_COSIGN_INVOKER   2
#define FL_KQ_COSIGN_SIGN      3
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
#endif /* FL_KQ_H */

#endif
