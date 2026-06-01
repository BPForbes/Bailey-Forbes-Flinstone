/**
 * **P5-7 — Host catalog (SQLite)** (module contract, normative).
 *
 * When a file or message reaches the session **host**, metadata and payload bytes
 * are indexed in **`server_shared/host_catalog.db`**. Lookups use a **SHA-256
 * content hash** (hex); **transfer_id** (share_id / message correlation id)
 * ties in-flight wire frames to catalog rows until completion.
 *
 * **Distribution:** host relay stores at the hub, then continues the chain to
 * recipients. On host promotion the **`server_shared/`** tree (DB + **`blobs/`**)
 * is adopted by the successor (same cwd) or imported via handoff copy. After the
 * host stops, members fetch blobs they are allowed to read using hash + member_id.
 */
#ifndef FL_CONTRACT_P5_SERVER_CATALOG_H
#define FL_CONTRACT_P5_SERVER_CATALOG_H

#include "contract_p3_channel_sidecar.h"
#include "contract_p5_file_delivery.h"

#include <stddef.h>
#include <stdint.h>

#define FL_CONTRACT_P5_7_SERVER_CATALOG_CONTRACT_DEFINED 1

/** Relative to cwd: `server_shared/host_catalog.db`. */
#define FL_SERVER_CATALOG_DB_NAME           "host_catalog.db"
#define FL_SERVER_CATALOG_BLOBS_DIR_NAME    "blobs"

/** SHA-256 digest as lowercase hex (+ NUL). */
#define FL_SERVER_CATALOG_HASH_HEX_MAX      65u

/** Max rows returned by list helpers in one call. */
#define FL_SERVER_CATALOG_LIST_MAX          256u

typedef enum fl_server_catalog_status {
    FL_SERVER_CATALOG_PENDING  = 0,
    FL_SERVER_CATALOG_COMPLETE = 1,
    FL_SERVER_CATALOG_REVOKED  = 2,
    FL_SERVER_CATALOG_EXPIRED  = 3
} fl_server_catalog_status_t;

/** One catalog row (file or message). */
typedef struct fl_server_catalog_entry {
    char content_hash[FL_SERVER_CATALOG_HASH_HEX_MAX];
    char transfer_id[FL_SERVER_SHARE_ID_MAX];
    uint8_t payload_kind; /* FL_CHANNEL_PAYLOAD_FILE or FL_CHANNEL_PAYLOAD_MSG */
    uint16_t sender_member_id;
    uint16_t receiver_member_id; /* 0 = public / broadcast */
    fl_file_perms_t file_perms;
    uint8_t is_public;
    uint64_t created_at;
    uint64_t expires_at;
    char payload_name[FL_SERVER_FILE_NAME_MAX];
    char storage_relpath[FL_SERVER_SHARE_REL_PATH_MAX];
    fl_server_catalog_status_t status;
    uint64_t total_bytes;
} fl_server_catalog_entry_t;

fl_result_t fl_server_catalog_open(void);
fl_result_t fl_server_catalog_close(void);
fl_result_t fl_server_catalog_checkpoint(void);

fl_result_t fl_server_catalog_register_file_offer(const fl_server_file_offer_t *offer);
fl_result_t fl_server_catalog_commit_file_offer(const fl_server_file_offer_t *offer,
                                                const uint8_t *data,
                                                size_t data_len);
fl_result_t fl_server_catalog_revoke_transfer(const char *transfer_id);

fl_result_t fl_server_catalog_store_message(uint16_t sender_member_id,
                                            uint16_t receiver_member_id,
                                            const char *payload_name,
                                            const uint8_t *body,
                                            size_t body_len,
                                            uint64_t expires_at,
                                            fl_file_perms_t file_perms,
                                            char *out_transfer_id,
                                            size_t out_transfer_id_cap,
                                            char *out_content_hash,
                                            size_t out_hash_cap);

fl_result_t fl_server_catalog_member_can_fetch(const fl_server_catalog_entry_t *entry,
                                               uint16_t member_id,
                                               uint64_t now);

fl_result_t fl_server_catalog_lookup_transfer(const char *transfer_id,
                                              fl_server_catalog_entry_t *out);

fl_result_t fl_server_catalog_fetch_by_hash(const char *content_hash_hex,
                                          uint16_t member_id,
                                          uint64_t now,
                                          fl_server_catalog_entry_t *out_meta,
                                          uint8_t *out_data,
                                          size_t out_data_cap,
                                          size_t *out_data_len);

fl_result_t fl_server_catalog_read_blob(const fl_server_catalog_entry_t *entry,
                                        uint8_t *out_data,
                                        size_t out_data_cap,
                                        size_t *out_data_len);

fl_result_t fl_server_catalog_list_for_member(uint16_t member_id,
                                            uint64_t now,
                                            fl_server_catalog_entry_t *out,
                                            size_t out_cap,
                                            size_t *out_count);

fl_result_t fl_server_catalog_purge_expired(uint64_t now);

/** Copy `host_catalog.db` + `blobs/` from another `server_shared` root (handoff). */
fl_result_t fl_server_catalog_import_handoff(const char *src_server_shared_dir);

#endif /* FL_CONTRACT_P5_SERVER_CATALOG_H */
