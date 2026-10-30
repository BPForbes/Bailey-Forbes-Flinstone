/**
 * **P5-7 — Host catalog (SQLite)** (module contract, normative).
 *
 * When a file or message reaches the session **host**, metadata and payload bytes
 * are indexed in **`server_shared/host_catalog.db`**. Lookups use a **SHA-256
 * content hash** (hex); **transfer_id** (share_id / message correlation id)
 * ties in-flight wire frames to catalog rows until completion. Multiple rows may
 * share the same **content_hash** when bytes match; ACL is per **transfer_id**.
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

/** Max blob bytes the catalog stores per row (matches file delivery cap). */
#define FL_SERVER_CATALOG_MAX_BYTES (16u * 1024u * 1024u)

/** Max rows returned by fl_server_catalog_list_for_member in one call. */
#define FL_SERVER_CATALOG_LIST_MAX    64u

/** Schema revision stamped in the catalog file (PRAGMA user_version); a newer file is refused. */
#define FL_SERVER_CATALOG_SCHEMA_VERSION 1

/** Catalog file mode: owner read/write only (its journal files get the same mode from SQLite). */
#define FL_SERVER_CATALOG_FILE_MODE 0600

/** How long an open or a write waits for another process holding the catalog (milliseconds). */
#define FL_SERVER_CATALOG_BUSY_TIMEOUT_MS 5000

/** Largest source catalog file a handoff import copies (bytes). */
#define FL_SERVER_CATALOG_MAX_DB_BYTES (256ull * 1024ull * 1024ull)

/** How long a handoff probe of a catalog file waits before it fails closed (milliseconds). */
#define FL_SERVER_CATALOG_PROBE_TIMEOUT_MS 250

typedef enum fl_server_catalog_status {
    FL_SERVER_CATALOG_PENDING  = 0,
    FL_SERVER_CATALOG_COMPLETE = 1,
    FL_SERVER_CATALOG_REVOKED  = 2,
    FL_SERVER_CATALOG_EXPIRED  = 3,
    /* KeyQuorum delivery rows (FL_CHANNEL_PAYLOAD_KQ, kq_delivery.c), never on the share path.
     * Stored values: append, never renumber. EXPIRED (3) also ends a KQ row. */
    FL_SERVER_CATALOG_KQ_HELD      = 4, /* letter held, waiting for the co-signature */
    FL_SERVER_CATALOG_KQ_RELEASED  = 5, /* co-signed and released, waiting for the receipt */
    FL_SERVER_CATALOG_KQ_DELIVERED = 6  /* receipt in, letter deleted */
} fl_server_catalog_status_t;

/** One catalog row (file or message). */
typedef struct fl_server_catalog_entry {
    char content_hash[FL_SERVER_CATALOG_HASH_HEX_MAX];
    char transfer_id[FL_SERVER_SHARE_ID_MAX];
    uint8_t payload_kind; /* FILE or MSG; a stored value of any other kind (the reserved FL_CHANNEL_PAYLOAD_KQ, or out of range, read as 0) is never returned by the share path */
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

/** Pending row from in-flight MSG_META (host relay); completed on matching MSG. */
fl_result_t fl_server_catalog_register_message_pending(const char *transfer_id,
                                                       uint16_t sender_member_id,
                                                       uint16_t receiver_member_id,
                                                       uint64_t expires_at,
                                                       fl_file_perms_t file_perms,
                                                       const char *payload_name);

/** Complete a pending message row with body bytes (transfer_id from MSG_META). */
fl_result_t fl_server_catalog_commit_message(const char *transfer_id,
                                             const uint8_t *body,
                                             size_t body_len);

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

/** Read blob bytes; out_data_cap must be >= entry->total_bytes for complete rows. */
fl_result_t fl_server_catalog_read_blob(const fl_server_catalog_entry_t *entry,
                                        uint8_t *out_data,
                                        size_t out_data_cap,
                                        size_t *out_data_len);

fl_result_t fl_server_catalog_list_for_member(uint16_t member_id,
                                            uint64_t now,
                                            fl_server_catalog_entry_t *out,
                                            size_t out_cap,
                                            size_t *out_count);

/** Expires COMPLETE rows of the share path's kinds (FILE, MSG) past their time; a row of the reserved
 *  kind is left to its own owner and neither changes nor gains an event here. */
fl_result_t fl_server_catalog_purge_expired(uint64_t now);

/**
 * One recorded event on a share. The table catalog_event is append-only: each row says what
 * happened to a share (offered, completed, accepted, declined, revoked, expired) and who did
 * it. Unlike catalog_entry.status, an event is never overwritten.
 */
typedef struct fl_server_catalog_event {
    uint32_t seq;
    char     event[16];
    uint16_t actor_member_id;
    uint64_t at;
    char     detail[FL_SERVER_FILE_NAME_MAX]; /* a content hash (64 hex) or a file name */
} fl_server_catalog_event_t;

/**
 * One unit of work on the catalog: a state change and its event. begin/commit/rollback bracket
 * it; a failure in either step is returned, so the caller can roll back and report it.
 */
fl_result_t fl_server_catalog_begin(void);
fl_result_t fl_server_catalog_commit(void);
void fl_server_catalog_rollback(void);

/** Appends one event to a share's history; seq is the next number for that share. FL_RESULT_ACCES
 *  (nothing written) when the id belongs to a row of the reserved kind. */
fl_result_t fl_server_catalog_append_event(const char *transfer_id, const char *event,
                                           uint16_t actor_member_id, uint64_t at,
                                           const char *detail);

/** The events of one share, oldest first, at most out_cap; out_count is what was copied (0 for a
 *  row of the reserved kind, as for an unknown id). */
fl_result_t fl_server_catalog_list_events(const char *transfer_id,
                                          fl_server_catalog_event_t *out, size_t out_cap,
                                          size_t *out_count);

/**
 * Copy `host_catalog.db` + `blobs/` from another `server_shared` root (handoff). The source file
 * (at most FL_SERVER_CATALOG_MAX_DB_BYTES) is copied once to a uniquely named owner-only temp file
 * next to the live one; that copy is what is checked, what the blobs are read from, and what is
 * renamed into place. Fails closed: a locked, corrupt or non-database file on either side is
 * FL_RESULT_ERR (so a corrupt live catalog is removed first, not repaired by importing over it),
 * as is a journal, -wal or -shm file beside either database, a missing or unreadable blob, or a
 * `blobs` directory that is a link. Each blob must be named blobs/<its SHA-256> and match it.
 * FL_RESULT_ACCES when either side holds a row of the reserved FL_CHANNEL_PAYLOAD_KQ kind (a
 * policy refusal that waiting does not clear), FL_RESULT_INVAL when either file is from a newer
 * schema (the live file is never downgraded) or a blob path or digest does not match. Nothing
 * replaces the live file in any of those cases. Single-writer rule: the host is quiet while an
 * import runs (two imports do not share a temp name, the last rename wins).
 * A catalog file that is a symlink or has more than one name is refused by
 * fl_server_catalog_open, so a catalog moved elsewhere behind a link must be moved back.
 */
fl_result_t fl_server_catalog_import_handoff(const char *src_server_shared_dir);

#endif /* FL_CONTRACT_P5_SERVER_CATALOG_H */
