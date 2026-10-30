#ifndef FL_DRIVER_KEYQUORUM_H
#define FL_DRIVER_KEYQUORUM_H

/*
 * KeyQuorum security driver ops (contracts/drivers/contract_p4_keyquorum.h, P4-9).
 *
 * Callers obtain the driver through the registry (fl_kq_driver_get). The driver is the only
 * caller of KeyQuorum's files ABI and of its account ABI
 * (keyquorum_identity.h, kq_account_*). Every file op returns KeyQuorum's own status
 * (KQ_FILE_* from keyquorum_files.h), every account op KQ_ACCOUNT_* so the kernel orchestrator
 * (kq_checkpoint.c) decides what each outcome means; the driver only sizes buffers,
 * wipes secrets with the ASM layer and compares in constant time.
 *
 * Buffers: an op that produces bytes allocates fl_kq_buf_t::data; the caller releases it
 * with fl_kq_buf_free, which wipes before freeing (containers hold file content).
 */

#include "contract_p4_keyquorum.h"
#include "fl/driver/driver.h"
#include "keyquorum_files.h"
#include "keyquorum_identity.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A KeyQuorum byte buffer. data is MEM_DOMAIN_FS memory (the driver allocates it, and so do the
 * file readers that fill one), released only with fl_kq_buf_free, which wipes it first. */
typedef struct fl_kq_buf {
    uint8_t *data;
    size_t   len;
} fl_kq_buf_t;

void fl_kq_buf_free(fl_kq_buf_t *buf);

/*
 * A KeyQuorum driver is a Flinstone driver: its first member is the generic descriptor
 * (fl/driver/driver.h: class FL_DRV_CLASS_SECURITY, name and synthetic id "keyquorum", and
 * the lifecycle ops whose probe checks the library ABI), so the driver registry holds it like
 * any other driver. The members after it are what this driver adds: the KeyQuorum ops.
 */
typedef struct fl_kq_driver fl_kq_driver_t;
struct fl_kq_driver {
    fl_driver_desc_t base;

    /* Tracked file. */
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

    /* Encoder: the public offer and the letter sealed to the recipient. */
    int (*offer)(const fl_kq_buf_t *c, const uint8_t rev[32], kq_file_offer_t *out);
    int (*seal)(const fl_kq_buf_t *c, const uint8_t rev[32], const kq_file_trust_t *trust,
                const char *sender, const uint8_t sender_secret[32],
                const uint8_t sender_box_public[32], const char *recipient,
                const uint8_t recipient_box_public[32], fl_kq_buf_t *letter,
                uint8_t delivery_id[16]);

    /* Latent space (host) and co-signer: signatures over the public offer only. */
    int (*offer_verify)(const kq_file_offer_t *offer, const char *author,
                        const uint8_t author_public[32]);
    int (*cosign)(const kq_file_offer_t *offer, const char *coauthor, const uint8_t secret[32],
                  uint8_t sig[64]);
    int (*cosign_verify)(const kq_file_offer_t *offer, const char *coauthor,
                         const uint8_t coauthor_public[32], const uint8_t sig[64]);

    /* Decoder. */
    int (*open)(const fl_kq_buf_t *letter, const uint8_t box_secret[32], const char *sender,
                const char *recipient, const uint8_t sender_public[32], fl_kq_buf_t *c,
                uint8_t rev[32], uint8_t root[32]);
    int (*attach)(const fl_kq_buf_t *c, const kq_file_offer_t *offer, const char *coauthor,
                  const uint8_t sig[64], fl_kq_buf_t *out);
    int (*readiness)(const fl_kq_buf_t *c, const uint8_t rev[32], const char *recipient,
                     const kq_file_trust_t *trust, uint32_t *have, uint32_t *need);
    int (*freshness)(const uint8_t (*seen)[64], size_t nseen, const fl_kq_buf_t *c,
                     const uint8_t rev[32], const uint8_t root[32]);

    /* Editor's permit: a supervisor countersigned rev (KQ_FILE_OK) or did not (KQ_FILE_UNTRUSTED).
     * The checkpoint requires it before a newer copy of a file it holds is accepted. */
    int (*permits_newer)(const fl_kq_buf_t *c, const uint8_t rev[32],
                         const kq_file_trust_t *trust);
    /* A supervisor adds a countersignature over rev; out is the container with it. */
    int (*countersign)(const fl_kq_buf_t *c, const uint8_t rev[32], const char *supervisor,
                       const uint8_t secret[32], const kq_file_trust_t *trust, fl_kq_buf_t *out);
    /* History, offline: one tab-separated line per event (see keyquorum_files.h). */
    int (*events)(const fl_kq_buf_t *c, fl_kq_buf_t *out);

    /* ASM layer. */
    void (*wipe)(void *secret, size_t n);
    int (*equal)(const void *a, const void *b, size_t n);

    /*
     * Account keys (keyquorum_identity.h), appended after equal. One-to-one over kq_account_*:
     * fixed-size buffers owned by the caller (FL_KQ_ACCOUNT_* lengths), nothing allocated,
     * nothing retained, the KQ_ACCOUNT_* status returned unchanged (the caller maps
     * WRONG_PASSWORD). The caller wipes every secret buffer with wipe. The account ops have no
     * ABI symbol of their own: they are covered by the same probe as the file ops
     * (FL_KQ_ABI_VERSION), because both come from the one linked archive.
     */
    int (*account_keygen)(uint8_t secret[32], uint8_t pub[32]);
    int (*account_public)(const uint8_t secret[32], uint8_t pub[32]);
    int (*account_box_keygen)(uint8_t secret[32], uint8_t pub[32]);
    int (*account_box_public)(const uint8_t secret[32], uint8_t pub[32]);
    /* Seals secret under password into out (cap bytes); *out_len is FL_KQ_ACCOUNT_SEALED_LEN. */
    int (*account_seal)(const char *password, const uint8_t secret[32], uint8_t *out, size_t cap,
                        size_t *out_len);
    int (*account_open)(const char *password, const uint8_t *sealed, size_t len,
                        uint8_t secret[32]);
    /* role is FL_KQ_ACCOUNT_BRIDGE_REQUEST or _ACCEPT. */
    int (*account_bridge_sign)(int role, const char *from, const char *to, const char *signer,
                               const uint8_t nonce[16], const uint8_t secret[32],
                               uint8_t sig[64]);
    /* 1 when the signature is valid, 0 when not (a negative status for a malformed call). */
    int (*account_bridge_verify)(int role, const char *from, const char *to, const char *signer,
                                 const uint8_t nonce[16], const uint8_t pub[32],
                                 const uint8_t sig[64]);

    /*
     * History names (file ABI version 2), appended after the account ops. The name of a history
     * event type or outcome code as KeyQuorum spells it: a static string the caller never frees,
     * or NULL for a code the linked library does not know. For display only; nothing parses them.
     */
    const char *(*event_name)(uint8_t code);
    const char *(*outcome_name)(uint8_t code);
};

/**
 * The registered KeyQuorum driver, resolved by the driver model (driver_model.c, the one
 * resolver): the registry must hold fl_kq_driver_desc() and the file ABI probe
 * (base.ops->probe, FL_KQ_ABI_VERSION) must return FL_RESULT_OK, checked on every call.
 * NULL when that descriptor is not registered (including an empty registry and after
 * drivers_shutdown) or when the probe does not accept. This is the only caller-facing
 * accessor. It matches the registered pointer, not the driver name. Account ops stay on
 * this driver and share that probe; they have no ABI symbol of their own.
 * NULL means fail closed (nothing is generated, sealed, opened or signed).
 */
const fl_kq_driver_t *fl_kq_driver_get(void);
/**
 * fl_kq_driver_get() for callers that can run before drivers_init (user and bridge stores,
 * the session seed, the browser lab, unit tests): registers fl_kq_driver_desc() once through
 * fl_driver_registry_ensure, then returns fl_kq_driver_get(). After drivers_shutdown it
 * registers nothing and returns NULL, so it fails closed like fl_kq_driver_get().
 */
const fl_kq_driver_t *fl_kq_driver_get_or_register(void);
/** The driver as its base descriptor, for fl_driver_registry_register. */
const fl_driver_desc_t *fl_kq_driver_desc(void);
/** Unchecked cast from the base descriptor back to the KeyQuorum driver (base is the first
 *  member). It does not consult the registry and it does not probe. Use it only after the
 *  pointer is already known to be fl_kq_driver_desc(). Account callers use fl_kq_driver_get(). */
static inline const fl_kq_driver_t *fl_kq_driver_from_desc(const fl_driver_desc_t *desc) {
    return (const fl_kq_driver_t *)desc;
}

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_KEYQUORUM_H */
