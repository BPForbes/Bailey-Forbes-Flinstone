#ifndef FL_DRIVER_KEYQUORUM_H
#define FL_DRIVER_KEYQUORUM_H

/* The driver is the only caller of KeyQuorum's C ABI; callers wipe the secrets they pass. */

#include "contract_p4_keyquorum.h"
#include "fl/driver/driver.h"
#include "keyquorum_access.h"
#include "keyquorum_assembly.h"
#include "keyquorum_bridge.h"
#include "keyquorum_error.h"
#include "keyquorum_files.h"
#include "keyquorum_identity.h"
#include "keyquorum_slots.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* Released only with fl_kq_buf_free, which wipes it first. */
typedef struct fl_kq_buf {
    uint8_t *data;
    size_t   len;
} fl_kq_buf_t;

void fl_kq_buf_free(fl_kq_buf_t *buf);

typedef struct fl_kq_driver fl_kq_driver_t;
struct fl_kq_driver {
    fl_driver_desc_t base;

    int (*file_new)(const uint8_t file_id[16], const char *name, const char *scope_root,
                    fl_kq_buf_t *out);
    int (*check_in)(const fl_kq_buf_t *c, const char *author, const char *created_at_utc,
                    const uint8_t *payload, size_t len, const uint8_t secret[32],
                    fl_kq_buf_t *out, uint8_t rev[32]);
    int (*verify)(const fl_kq_buf_t *c, uint8_t root[32]);
    int (*head)(const fl_kq_buf_t *c, uint8_t rev[32]);
    int (*payload)(const fl_kq_buf_t *c, const uint8_t rev[32], fl_kq_buf_t *out);
    int (*signers)(const fl_kq_buf_t *c, fl_kq_buf_t *out);
    int (*merge)(const fl_kq_buf_t *local, const fl_kq_buf_t *incoming, const char *actor,
                 const char *occurred_at_utc, fl_kq_buf_t *out);
    int (*select_read)(const fl_kq_buf_t *c, const kq_file_trust_t *trust,
                       const uint8_t (*accepted)[32], size_t naccepted, uint8_t rev[32]);

    int (*offer)(const fl_kq_buf_t *c, const uint8_t rev[32], kq_file_offer_t *out);
    int (*seal)(const fl_kq_buf_t *c, const uint8_t rev[32], const kq_file_trust_t *trust,
                const char *sender, const uint8_t sender_secret[32],
                const uint8_t sender_box_public[32], const char *recipient,
                const uint8_t recipient_box_public[32], fl_kq_buf_t *letter,
                uint8_t delivery_id[16]);

    int (*offer_verify)(const kq_file_offer_t *offer, const char *author,
                        const uint8_t author_public[32]);
    int (*cosign)(const kq_file_offer_t *offer, const char *coauthor, const uint8_t secret[32],
                  uint8_t sig[64]);
    int (*cosign_verify)(const kq_file_offer_t *offer, const char *coauthor,
                         const uint8_t coauthor_public[32], const uint8_t sig[64]);

    int (*open)(const fl_kq_buf_t *letter, const uint8_t box_secret[32], const char *sender,
                const char *recipient, const uint8_t sender_public[32], fl_kq_buf_t *c,
                uint8_t rev[32], uint8_t root[32]);
    int (*attach)(const fl_kq_buf_t *c, const kq_file_offer_t *offer, const char *coauthor,
                  const uint8_t sig[64], fl_kq_buf_t *out);
    int (*readiness)(const fl_kq_buf_t *c, const uint8_t rev[32], const char *recipient,
                     const kq_file_trust_t *trust, uint32_t *have, uint32_t *need);
    int (*freshness)(const uint8_t (*seen)[64], size_t nseen, const fl_kq_buf_t *c,
                     const uint8_t rev[32], const uint8_t root[32]);

    /* KQ_FILE_OK or KQ_FILE_UNTRUSTED; the checkpoint requires it before accepting a newer copy. */
    int (*permits_newer)(const fl_kq_buf_t *c, const uint8_t rev[32],
                         const kq_file_trust_t *trust);
    int (*countersign)(const fl_kq_buf_t *c, const uint8_t rev[32], const char *supervisor,
                       const uint8_t secret[32], const kq_file_trust_t *trust, fl_kq_buf_t *out);
    int (*events)(const fl_kq_buf_t *c, fl_kq_buf_t *out);

    void (*wipe)(void *secret, size_t n);
    int (*equal)(const void *a, const void *b, size_t n);

    /* Fixed-size caller buffers; KQ_ACCOUNT_* returned unchanged; caller wipes every secret. */
    int (*account_keygen)(uint8_t secret[32], uint8_t pub[32]);
    int (*account_public)(const uint8_t secret[32], uint8_t pub[32]);
    int (*account_box_keygen)(uint8_t secret[32], uint8_t pub[32]);
    int (*account_box_public)(const uint8_t secret[32], uint8_t pub[32]);
    int (*account_seal)(const char *password, const uint8_t secret[32], uint8_t *out, size_t cap,
                        size_t *out_len);
    int (*account_open)(const char *password, const uint8_t *sealed, size_t len,
                        uint8_t secret[32]);
    int (*account_bridge_sign)(int role, const char *from, const char *to, const char *signer,
                               const uint8_t nonce[16], const uint8_t secret[32],
                               uint8_t sig[64]);
    int (*account_bridge_verify)(int role, const char *from, const char *to, const char *signer,
                                 const uint8_t nonce[16], const uint8_t pub[32],
                                 const uint8_t sig[64]);

    const char *(*event_name)(uint8_t code);
    const char *(*outcome_name)(uint8_t code);

    int (*seal_snapshot)(const fl_kq_buf_t *c, uint32_t upto_events, const char *sender,
                         const uint8_t sender_secret[32], const uint8_t sender_box_public[32],
                         const char *recipient, const uint8_t recipient_box_public[32],
                         fl_kq_buf_t *letter, uint8_t delivery_id[16]);
    int (*open_snapshot)(const fl_kq_buf_t *letter, const uint8_t box_secret[32],
                         const char *expected_sender, const char *expected_recipient,
                         const uint8_t sender_public[32], fl_kq_buf_t *snap, uint8_t root[32],
                         uint32_t *event_count);
    int (*snapshot_covers)(const fl_kq_buf_t *extract, const fl_kq_buf_t *snapshot);
    int (*error_sign)(uint16_t code, uint8_t class_code, uint8_t step, const uint8_t object_ref[32],
                      const char *actor_label, const char *occurred_at, const char *detail_key,
                      const char *detail_value, const uint8_t signing_secret[32], fl_kq_buf_t *out);
    int (*error_inject)(const fl_kq_buf_t *kqtf, const fl_kq_buf_t *kqer,
                        const uint8_t reporter_public[32], const uint8_t pkg_sha256[32],
                        fl_kq_buf_t *out);
    const char *(*error_name)(uint16_t code);
    const char *(*error_class_name)(uint8_t class_code);

    int (*slot_open)(const uint8_t *kqst, size_t klen, const char *label, const char *passphrase,
                     const uint8_t expected_device_id[16], const uint8_t expected_signing_pub[32],
                     const uint8_t expected_encryption_pub[32], uint8_t signing_secret_out[32],
                     uint8_t encryption_secret_out[32]);

    int (*head_author)(const fl_kq_buf_t *c, char *out, size_t cap);

    int (*access_requirement)(const char *current, const char *target, int sudo,
                              const kq_bridge_t *live_bridges, size_t nbridges, int parent_approval,
                              int *bridge_index_out, int *needs_parent_grant_out);
    int (*bridge_prove)(const char *uid, uint32_t generation, const uint8_t salt[16],
                        const uint8_t bridge_public[32], const char *local_label,
                        const uint8_t *sealed_bridge_key, size_t slen, const uint8_t wrap_salt[16],
                        const uint8_t encryption_sk[32], const uint8_t signing_sk[32],
                        const uint8_t *challenge, size_t clen, uint8_t *artifact_out, size_t cap,
                        size_t *artifact_len);
    int (*bridge_verify)(const char *uid, uint32_t generation, const uint8_t salt[16],
                         const uint8_t bridge_public[32], int destroyed,
                         const char *const *member_labels, const uint8_t *member_signing_pubs,
                         size_t nmembers, const char *verifier_label, const uint8_t *challenge,
                         size_t clen, const uint8_t *artifact, size_t alen);

    int (*bridge_connects)(const char *end_a, const char *end_b, const char *actor,
                           const char *scope);

    unsigned (*assembly_threshold)(unsigned holders);
    int (*assembly_split)(const uint8_t seed[32], unsigned generation,
                          const char *const *child_labels, const uint8_t (*child_box_public)[32],
                          size_t nchildren, uint8_t *out, size_t cap, size_t *out_len);
    int (*assembly_generation)(const uint8_t *assembly, size_t alen, unsigned *generation_out);
    int (*assembly_assemble)(const uint8_t *assembly, size_t alen,
                             const uint8_t parent_signing_public[32],
                             const char *const *unlocked_labels,
                             const uint8_t (*unlocked_box_secret)[32],
                             const uint8_t (*unlocked_device_id)[16], size_t nunlocked,
                             size_t minimum_devices, uint8_t seed_out[32]);

    int (*sudo_sign)(int role, const char *invoker, const char *authority, const uint8_t *action,
                     size_t action_len, const uint8_t nonce[16], const uint8_t secret[32],
                     uint8_t sig_out[64]);
    int (*sudo_verify)(const char *invoker, const char *authority, const uint8_t *action,
                       size_t action_len, const uint8_t nonce[16], const uint8_t invoker_public[32],
                       const uint8_t invoker_sig[64], const uint8_t authority_public[32],
                       const uint8_t authority_sig[64]);
    int (*slot_seal)(const char *label, const char *passphrase, const uint8_t device_id[16],
                     const uint8_t signing_secret[32], const uint8_t encryption_secret[32],
                     uint8_t *token_out, size_t cap, size_t *token_len,
                     uint8_t signing_public_out[32], uint8_t encryption_public_out[32]);
};

/* NULL when the descriptor is not registered or the probe refuses: fail closed. */
const fl_kq_driver_t *fl_kq_driver_get(void);
const fl_kq_driver_t *fl_kq_driver_get_or_register(void);
const fl_driver_desc_t *fl_kq_driver_desc(void);
/* Unchecked cast: only after the pointer is known to be fl_kq_driver_desc(). */
static inline const fl_kq_driver_t *fl_kq_driver_from_desc(const fl_driver_desc_t *desc) {
    return (const fl_kq_driver_t *)desc;
}

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_KEYQUORUM_H */
