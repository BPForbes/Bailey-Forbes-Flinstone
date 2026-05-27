/**
 * **P5-6 — Cross-user file delivery** (module contract, normative).
 *
 * **Distribution:** describes how a **server** session moves file bytes and metadata
 * between members. The shell **`server send`** command is implemented in a later PR;
 * this shard freezes the interchange record the VFS and net layers must respect.
 */
#ifndef FL_CONTRACT_P5_FILE_DELIVERY_H
#define FL_CONTRACT_P5_FILE_DELIVERY_H

#include "contract_p5_server_share.h"

#include <stdint.h>

#define FL_CONTRACT_P5_6_FILE_DELIVERY_CONTRACT_DEFINED 1

/** Recipient disposition flags (binary 0/1 in interchange; may be packed in wire flags). */
typedef struct {
    /** **1** — write through to the path captured on the recipient (overwrite). */
    uint8_t overwrite_existing;
    /** **1** — place under **FL_SERVER_SHARE_DIR_NAME** when no file exists at path. */
    uint8_t use_server_share;
    /** **1** — recipient explicitly declines storage (no share, no overwrite). */
    uint8_t decline_storage;
} fl_server_file_disposition_t;

/**
 * File offer metadata exchanged before chunk payloads (**P3** **FILE_OFFER** opcode).
 * **sender_path** is the resolved path on the sender at send time (NUL-terminated in C
 * interchange; on-wire encoding may be length-prefixed UTF-8).
 */
typedef struct {
    char sender_path[FL_SERVER_SHARE_SENDER_PATH_MAX];
    char sender_principal[64];
    char recipient_principal[64];
    uint64_t file_size;
    uint32_t sender_member_id;
    fl_server_file_disposition_t disposition;
} fl_server_file_offer_t;

/** Max bytes per **FILE_CHUNK** payload on the session wire. */
#ifndef FL_SERVER_FILE_CHUNK_MAX
#define FL_SERVER_FILE_CHUNK_MAX 4096u
#endif

_Static_assert(FL_SERVER_FILE_CHUNK_MAX >= 512u, "file chunk cap unexpectedly small");

#endif /* FL_CONTRACT_P5_FILE_DELIVERY_H */
