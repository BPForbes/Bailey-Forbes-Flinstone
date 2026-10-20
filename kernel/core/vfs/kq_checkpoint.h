#ifndef FL_KQ_CHECKPOINT_H
#define FL_KQ_CHECKPOINT_H

/*
 * KeyQuorum security checkpoints (contracts/drivers/contract_p4_keyquorum.h).
 *
 * The kernel orchestrator for co-signed delivery: it decides which checkpoint runs and
 * what an outcome means, and calls only the KeyQuorum driver (fl/driver/keyquorum.h) and
 * the account store (user_db.h) for keys. No network here; net_kq_delivery.c carries the
 * results between parties.
 *
 *   encode   (sender)    check in + sign, public offer, letter sealed to the recipient
 *   admit    (host)      sender's signature on the offer; recipient's co-signature
 *   cosign   (recipient) co-sign the public offer
 *   decode   (recipient) open, attach own co-signature, full readiness, freshness, write
 *
 * Secrets are passed in by the caller (opened from user_db with the account password)
 * and stay the caller's to wipe; every buffer that held content is wiped before free.
 */

#include "contract_result.h"
#include "fl/driver/keyquorum.h"
#include "user_db.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FL_KQ_CP_OK = 0,
    FL_KQ_CP_NO_DRIVER,
    FL_KQ_CP_NO_KEY,
    FL_KQ_CP_BAD_SIGNATURE,
    FL_KQ_CP_NOT_RECIPIENT,
    FL_KQ_CP_UNTRUSTED,
    FL_KQ_CP_FORKED,
    FL_KQ_CP_BELOW_THRESHOLD,
    FL_KQ_CP_REPLAYED,
    FL_KQ_CP_NOT_NEWER,
    FL_KQ_CP_EXISTS,
    FL_KQ_CP_TOO_LARGE,
    FL_KQ_CP_IO,
    FL_KQ_CP_INVALID,
} fl_kq_cp_result_t;

const char *fl_kq_cp_reason(fl_kq_cp_result_t r);
fl_result_t fl_kq_cp_to_result(fl_kq_cp_result_t r);

/**
 * True when name is a plain file name a delivery may use: not empty, not "." or "..", and
 * free of '/', '\\' and control bytes. Every name that arrives over the wire (offer,
 * announcement) is checked before it is stored, printed or joined to a directory.
 */
int fl_kq_cp_safe_name(const char *name);

/** What the sender's encoder produces for one delivery. */
typedef struct {
    kq_file_offer_t offer;
    fl_kq_buf_t     letter;
    uint8_t         delivery_id[FL_KQ_DELIVERY_ID_LEN];
    char            file_name[128];
} fl_kq_encoded_t;

void fl_kq_encoded_free(fl_kq_encoded_t *enc);

/** Sidecars beside a tracked file: its container and the (revision, root) pairs accepted. */
#define FL_KQ_CONTAINER_SUFFIX ".kqtf"
#define FL_KQ_SEEN_SUFFIX ".kqseen"

/**
 * Encode checkpoint (sender). Checks in the file at path as a new revision by sender_key
 * on the file's current head (its container is path + FL_KQ_CONTAINER_SUFFIX, created on
 * first use with sender_key as owner), signs it with sender_signing_secret, and seals it
 * to recipient_box_public. FORKED when the file has two heads.
 */
fl_kq_cp_result_t fl_kq_cp_encode(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_secret[32],
                                  const char *recipient_key,
                                  const uint8_t recipient_box_public[32], const char *path,
                                  const char *created_at_utc, fl_kq_encoded_t *out);

/** Latent checkpoint (host): the offer carries author_key's signature under the host registry. */
fl_kq_cp_result_t fl_kq_cp_admit_offer(const fl_user_db_t *users, const char *author_key,
                                       const kq_file_offer_t *offer);

/** Latent checkpoint (host): sig is recipient_key's co-signature on the offer. */
fl_kq_cp_result_t fl_kq_cp_admit_cosign(const fl_user_db_t *users, const char *recipient_key,
                                        const kq_file_offer_t *offer, const uint8_t sig[64]);

/** Co-sign checkpoint (recipient). */
fl_kq_cp_result_t fl_kq_cp_cosign(const kq_file_offer_t *offer, const char *me_key,
                                  const uint8_t signing_secret[32], uint8_t sig[64]);

/**
 * Decode checkpoint (recipient). Opens letter with box_secret (signed by sender_key under
 * sender_signing_public, addressed to me_key), attaches my_sig, and writes dest_path only
 * when KeyQuorum reports the delivery ready (author signature, trust, co-signature, single
 * head) and the copy is FIRST or NEWER than what dest_path's sidecars recorded. A file
 * already at dest_path with no sidecar is never overwritten (EXISTS).
 */
fl_kq_cp_result_t fl_kq_cp_decode(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_public[32], const char *me_key,
                                  const uint8_t box_secret[32], const fl_kq_buf_t *letter,
                                  const kq_file_offer_t *offer, const uint8_t my_sig[64],
                                  const char *dest_path);

#endif /* FL_KQ_CHECKPOINT_H */
