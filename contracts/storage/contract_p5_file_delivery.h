/**
 * **P5-6 — Cross-user file delivery** (module contract, normative).
 *
 * **Distribution:** describes how a native Flinstone **server file** session moves
 * file offers, chunks, completion state, receiver disposition, revocation, and
 * display snapshots between members. SFTP is a compatibility adapter over this
 * native contract, not the core file-transfer system.
 */
#ifndef FL_CONTRACT_P5_FILE_DELIVERY_H
#define FL_CONTRACT_P5_FILE_DELIVERY_H

#include "contract_pkt_channel_meta.h"
#include "contract_p3_channel_sidecar.h"
#include "contract_p5_file_perms.h"
#include "contract_p5_member_identity.h"
#include "contract_p5_server_share.h"

#include <stdint.h>

#define FL_CONTRACT_P5_6_FILE_DELIVERY_CONTRACT_DEFINED 1

#ifndef FL_SERVER_SHARE_ID_MAX
#define FL_SERVER_SHARE_ID_MAX      64u
#endif
#ifndef FL_SERVER_PATH_MAX
#define FL_SERVER_PATH_MAX          256u
#endif
#ifndef FL_SERVER_FILE_NAME_MAX
#define FL_SERVER_FILE_NAME_MAX     128u
#endif
#ifndef FL_SERVER_FILE_HASH_MAX
#define FL_SERVER_FILE_HASH_MAX     32u
#endif
#ifndef FL_SERVER_DISPLAY_MAX
#define FL_SERVER_DISPLAY_MAX       96u
#endif
#ifndef FL_SERVER_PRINCIPAL_MAX
#define FL_SERVER_PRINCIPAL_MAX     64u
#endif
#ifndef FL_SERVER_FILE_CHUNK_MAX
#define FL_SERVER_FILE_CHUNK_MAX    384u
#endif

/* FILE_OFFER metadata layout and revoke fast-path: contract_pkt_channel_meta.h */

typedef struct fl_bytes_view {
    const uint8_t *data;
    uint16_t len;
} fl_bytes_view_t;

typedef struct fl_bytes_writer {
    uint8_t *buf;
    uint16_t cap;
    uint16_t len;
} fl_bytes_writer_t;

static inline void fl_bytes_writer_init(fl_bytes_writer_t *w,
                                        uint8_t *buf,
                                        uint16_t cap)
{
    if (!w)
        return;

    w->buf = buf;
    w->cap = cap;
    w->len = 0u;
}

/** Offer metadata decoded from a FILE_OFFER payload. */
typedef struct fl_server_file_offer {
    char share_id[FL_SERVER_SHARE_ID_MAX];

    uint16_t sender_member_id;
    uint16_t receiver_member_id;   /* 0 means public/all. */

    fl_file_perms_t file_perms;
    uint16_t reserved0;

    uint64_t created_at;
    uint64_t expires_at;           /* 0 means never expires. */

    uint64_t file_size;
    uint32_t chunk_size;
    uint32_t total_chunks;

    char sender_path[FL_SERVER_PATH_MAX];
    char suggested_dest_path[FL_SERVER_PATH_MAX];
    char file_name[FL_SERVER_FILE_NAME_MAX];

    char sender_display[FL_SERVER_DISPLAY_MAX];
    char receiver_display[FL_SERVER_DISPLAY_MAX];

    char sender_principal[FL_SERVER_PRINCIPAL_MAX];
    char receiver_principal[FL_SERVER_PRINCIPAL_MAX];

    char sender_nick_snapshot[FL_SERVER_NICK_MAX];
    char receiver_nick_snapshot[FL_SERVER_NICK_MAX];

    uint8_t checksum[FL_SERVER_FILE_HASH_MAX];
} fl_server_file_offer_t;

typedef struct fl_server_file_chunk_header {
    char share_id[FL_SERVER_SHARE_ID_MAX];
    uint32_t chunk_index;
    uint32_t chunk_len;
    uint64_t file_offset;
} fl_server_file_chunk_header_t;

typedef struct fl_server_file_done {
    char share_id[FL_SERVER_SHARE_ID_MAX];
    uint64_t total_bytes;
    uint32_t total_chunks;
    uint8_t checksum[FL_SERVER_FILE_HASH_MAX];
    uint8_t status;
} fl_server_file_done_t;

/* fl_server_file_meta_t / fl_channel_sidecar_t: contract_p3_channel_sidecar.h */

fl_result_t fl_wire_put_bytes16(fl_bytes_writer_t *w,
                                const uint8_t *data,
                                uint16_t len);

fl_result_t fl_wire_get_bytes16(const uint8_t *buf,
                                uint16_t cap,
                                uint16_t *offset,
                                fl_bytes_view_t *out);

fl_result_t fl_server_file_offer_create(fl_server_file_offer_t *out,
                                        uint16_t sender_id,
                                        uint16_t receiver_id,
                                        const char *sender_path,
                                        const char *suggested_dest,
                                        fl_file_perms_t file_perms,
                                        uint64_t expires_at);

fl_result_t fl_server_file_offer_validate(const fl_server_file_offer_t *offer,
                                          uint16_t current_member_id,
                                          uint64_t now);

fl_result_t fl_server_file_offer_encode(const fl_server_file_offer_t *offer,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len);

fl_result_t fl_server_file_offer_decode(const uint8_t *payload,
                                        uint16_t payload_len,
                                        fl_server_file_offer_t *out);

fl_result_t fl_server_file_chunk_encode(const fl_server_file_chunk_header_t *chunk,
                                        const uint8_t *data,
                                        uint32_t data_len,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len);

fl_result_t fl_server_file_chunk_decode(const uint8_t *payload,
                                        uint16_t payload_len,
                                        fl_server_file_chunk_header_t *chunk,
                                        fl_bytes_view_t *data);

fl_result_t fl_server_file_meta_from_offer(const fl_server_file_offer_t *offer,
                                           fl_server_file_meta_t *out);

fl_result_t fl_server_file_meta_encode(const fl_server_file_meta_t *meta,
                                       uint8_t *out,
                                       uint16_t out_cap,
                                       uint16_t *out_len);

fl_result_t fl_server_file_meta_decode(const uint8_t *payload,
                                       uint16_t payload_len,
                                       fl_server_file_meta_t *out);

_Static_assert(FL_SERVER_FILE_CHUNK_MAX <= 4096u, "file chunk cap must fit bounded session buffers");
_Static_assert(FL_SERVER_SHARE_ID_MAX >= 32u, "share id cap too small");
_Static_assert(FL_SERVER_DISPLAY_MAX >= FL_SERVER_NICK_MAX, "display cap must include nick snapshots");

#endif /* FL_CONTRACT_P5_FILE_DELIVERY_H */
