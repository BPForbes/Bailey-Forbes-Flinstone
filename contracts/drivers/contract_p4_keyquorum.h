/**
 * **P4-9 — KeyQuorum security driver** (module contract, normative).
 *
 * **Distribution:** the driver (`kernel/drivers/keyquorum/kq_driver.c`, ops in
 * `kernel/include/fl/driver/keyquorum.h`) is the only code that calls KeyQuorum's C ABI
 * for files (`keyquorum_files.h`) and for accounts (`account_*`, `keyquorum_identity.h`).
 * `account_*` is driver-owned. Callers obtain the driver through the registry
 * (`fl_driver_registry_register` of `fl_kq_driver_desc()`, then `fl_kq_driver_get()`).
 * The driver owns no policy: the kernel orchestrator
 * (`kernel/core/vfs/kq_checkpoint.c`) decides when each checkpoint runs, and KeyQuorum
 * decides what a signature, a trusted revision and a fresh copy are. Secrets passed to the
 * driver are owned by the caller and wiped with **`asm_mem_secure_zero`**; digests and
 * signatures are compared with **`asm_mem_ct_equal`** (ASM layer, `mem_asm.h`).
 *
 * **Co-signed delivery** (author A to recipient B):
 * - **Encode** (A): check in and sign a revision, publish its public **offer** (no content),
 *   seal the revision to B's box key.
 * - **Latent** (host): verify A's signature on the offer against the host's account
 *   registry, hold the sealed letter it cannot open, announce the request to B.
 * - **Co-sign** (B): sign the public offer; the host verifies it the same way.
 * - **Fire**: the host releases the letter only when it holds
 *   **`FL_KQ_HOST_THRESHOLD`** valid signatures (A's and B's), before the delivery expires,
 *   and only once.
 * - **Decode** (B): open with B's box key, attach B's co-signature, and require KeyQuorum's
 *   full readiness (**`FL_KQ_RECEIVER_THRESHOLD`**: author signature, trust under the file's
 *   policy, co-signature), a single head, and a fresh copy before anything is written.
 *
 * **Outcomes:** every refusal maps to **`fl_result_t`** (**P0-2**) plus a checkpoint reason;
 * nothing is written on a refusal.
 */
#ifndef FL_CONTRACT_P4_KEYQUORUM_H
#define FL_CONTRACT_P4_KEYQUORUM_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_9_KEYQUORUM_CONTRACT_DEFINED 1

/** KeyQuorum files ABI this driver is built against (`KQ_FILE_ABI_VERSION`). */
#define FL_KQ_ABI_VERSION 2
/** Valid signatures the host needs before it releases a letter: author and recipient. */
#define FL_KQ_HOST_THRESHOLD 2u
/** Conditions the recipient needs before it writes the file (`kq_file_readiness`). */
#define FL_KQ_RECEIVER_THRESHOLD 3u
/** Seconds an unanswered co-signature request lives before it leaks away. */
#define FL_KQ_DELIVERY_TTL_SEC (24 * 60 * 60)
/** Seconds a released letter waits for the recipient's receipt before the host deletes it. */
#define FL_KQ_RELEASE_TTL_SEC (24 * 60 * 60)
/** Largest sealed letter the host holds (container plus envelope). */
#define FL_KQ_LETTER_MAX (4u * 1024u * 1024u)
/** Bytes of a public offer on the wire. */
#define FL_KQ_OFFER_WIRE_LEN 200u
/** Delivery id length (`KQ_FILE_DELIVERY_ID_LEN`). */
#define FL_KQ_DELIVERY_ID_LEN 16u


/* Account keys (`keyquorum_identity.h`): lengths, roles and statuses the driver's account ops
 * pass through unchanged. Copies, checked against KeyQuorum's by _Static_assert in kq_driver.c. */
#define FL_KQ_ACCOUNT_SECRET_LEN 32u
#define FL_KQ_ACCOUNT_PUBLIC_LEN 32u
#define FL_KQ_ACCOUNT_SIGNATURE_LEN 64u
#define FL_KQ_ACCOUNT_NONCE_LEN 16u
#define FL_KQ_ACCOUNT_SEALED_LEN 81u
#define FL_KQ_ACCOUNT_BRIDGE_REQUEST 1
#define FL_KQ_ACCOUNT_BRIDGE_ACCEPT 2
#define FL_KQ_ACCOUNT_OK 0
#define FL_KQ_ACCOUNT_INVALID (-1)
#define FL_KQ_ACCOUNT_NO_ROOM (-3)
#define FL_KQ_ACCOUNT_WRONG_PASSWORD (-4)

#endif /* FL_CONTRACT_P4_KEYQUORUM_H */
