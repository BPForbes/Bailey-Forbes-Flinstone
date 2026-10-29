#ifndef FL_DRIVER_KEYQUORUM_H
#define FL_DRIVER_KEYQUORUM_H

/*
 * KeyQuorum security driver ops (contracts/drivers/contract_p4_keyquorum.h, P4-9).
 *
 * The driver is the only caller of KeyQuorum's files ABI. Every op returns KeyQuorum's
 * own status (KQ_FILE_* from keyquorum_files.h) so the kernel orchestrator
 * (kq_checkpoint.c) decides what each outcome means; the driver only sizes buffers,
 * wipes secrets with the ASM layer and compares in constant time.
 *
 * Buffers: an op that produces bytes allocates fl_kq_buf_t::data; the caller releases it
 * with fl_kq_buf_free, which wipes before freeing (containers hold file content).
 */

#include "contract_p4_keyquorum.h"
#include "fl/driver/driver.h"
#include "keyquorum_files.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
};

/**
 * The driver, after a probe that the linked KeyQuorum library speaks FL_KQ_ABI_VERSION;
 * NULL when it does not (nothing is then signed, sealed or opened).
 *
 * Rule for a registry-based resolver (D11): a descriptor found through the registry or a bound
 * device must be compared with fl_kq_driver_desc() and the ABI re-probed before its ops are
 * used. fl_kq_driver_from_desc does neither; it is an unchecked cast.
 */
const fl_kq_driver_t *fl_kq_driver_get(void);
/** The driver as its base descriptor, for fl_driver_registry_register. */
const fl_driver_desc_t *fl_kq_driver_desc(void);
/** Back from the base descriptor to the KeyQuorum driver (base is the first member).
 *  Unchecked cast: it neither compares desc with fl_kq_driver_desc() nor probes the ABI, so a
 *  caller must do both (see fl_kq_driver_get) before using the result. */
static inline const fl_kq_driver_t *fl_kq_driver_from_desc(const fl_driver_desc_t *desc) {
    return (const fl_kq_driver_t *)desc;
}

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_KEYQUORUM_H */
