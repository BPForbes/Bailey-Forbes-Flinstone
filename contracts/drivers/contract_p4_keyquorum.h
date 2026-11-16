/* Secrets are owned and wiped by the caller (asm_mem_secure_zero); digests compare in constant time. */
#ifndef FL_CONTRACT_P4_KEYQUORUM_H
#define FL_CONTRACT_P4_KEYQUORUM_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_9_KEYQUORUM_CONTRACT_DEFINED 1

#define FL_KQ_ABI_VERSION 7
#define FL_KQ_HOST_THRESHOLD 2u
#define FL_KQ_RECEIVER_THRESHOLD 3u
#define FL_KQ_DELIVERY_TTL_SEC (24 * 60 * 60)
#define FL_KQ_RELEASE_TTL_SEC (24 * 60 * 60)
#define FL_KQ_LETTER_MAX (4u * 1024u * 1024u)
#define FL_KQ_OFFER_WIRE_LEN 200u
#define FL_KQ_DELIVERY_ID_LEN 16u

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
/* Append only; never renumber. Checked by _Static_assert in kq_driver.c. */
#define FL_KQ_SUDO_INVOKER 1
#define FL_KQ_SUDO_AUTHORITY 2
#define FL_KQ_SUDO_ACTION_MAX 4096u
#define FL_KQ_SLOT_TOKEN_LEN(n) ((size_t)(4 + 1 + 16 + 12 + 16 + 2 + 16 + 32 + 32) + (size_t)(n))

#define FL_KQ_ACCESS_NONE 0
#define FL_KQ_ACCESS_PASSWORD 1
#define FL_KQ_ACCESS_KQST_PASSWORD 2
#define FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST 3
#define FL_KQ_ACCESS_REFUSED 4
#define FL_KQ_ACCESS_INVALID (-1)
#define FL_KQ_ACCESS_MAX_BRIDGES 64u
#define FL_KQ_BRIDGE_MAX_MEMBERS 256u
#define FL_KQ_BRIDGE_SALT_LEN 16u
#define FL_KQ_BRIDGE_OK 0
#define FL_KQ_BRIDGE_INVALID (-1)
#define FL_KQ_BRIDGE_NO_ROOM (-3)
#define FL_KQ_BRIDGE_REFUSED (-5)
#define FL_KQ_BRIDGE_STALE (-6)

#endif /* FL_CONTRACT_P4_KEYQUORUM_H */
