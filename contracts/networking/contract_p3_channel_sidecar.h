/**
 * **P3-13 / P5-6 — Unified in-flight channel sidecar** (normative).
 *
 * One wire sidecar carries everything the relay layer needs to scan, authorize,
 * and route a **file** or **message** transfer while **Destination = 0**. The
 * sidecar is **not** persisted on the receiver's disk; relays strip it before the
 * final hop marks **FL_PKT_DESTINATION** (Byte 0).
 *
 * Tracks: **#239** (server session), **#279** (center_freq_hz when Wi-Fi lands),
 * **#297** / **contract_pkt_channel_meta.h** (5-byte MSB-first prefix).
 */
#ifndef FL_CONTRACT_P3_CHANNEL_SIDECAR_H
#define FL_CONTRACT_P3_CHANNEL_SIDECAR_H

#include "contract_pkt_channel_meta.h"
#include "contract_p5_server_share.h"

#include <stdint.h>

#ifndef FL_SERVER_SHARE_ID_MAX
#define FL_SERVER_SHARE_ID_MAX      64u
#endif
#ifndef FL_SERVER_FILE_NAME_MAX
#define FL_SERVER_FILE_NAME_MAX     128u
#endif

#define FL_CONTRACT_P3_CHANNEL_SIDECAR_DEFINED 1

/** Sidecar wire format revision (leading u8 after 5-byte prefix). */
#define FL_CHANNEL_SIDECAR_WIRE_REV 1u

#define FL_CHANNEL_PAYLOAD_FILE 1u
#define FL_CHANNEL_PAYLOAD_MSG  2u

/**
 * Complete in-flight sidecar: 5-byte scan prefix + transfer identity + policy.
 * **transfer_id** is share_id for files; message correlation id for chat payloads.
 * **payload_name** is file_name or a short message label (not full UTF-8 body).
 */
typedef struct fl_channel_sidecar {
    uint8_t  pkt_meta[FL_PKT_CHANNEL_META_LEN];
    uint16_t sender_member_id;
    uint16_t receiver_member_id;
    uint64_t expires_at;
    uint64_t center_freq_hz; /* 0 = unknown / wired; Wi-Fi center Hz when #279 resolves */
    uint8_t  payload_kind;   /* FL_CHANNEL_PAYLOAD_FILE or FL_CHANNEL_PAYLOAD_MSG */
    uint8_t  wire_rev;       /* FL_CHANNEL_SIDECAR_WIRE_REV on encode */
    char     transfer_id[FL_SERVER_SHARE_ID_MAX];
    char     payload_name[FL_SERVER_FILE_NAME_MAX];
} fl_channel_sidecar_t;

/** Legacy slim file meta — subset view for backward-compatible decode. */
typedef fl_channel_sidecar_t fl_server_file_meta_t;

#endif /* FL_CONTRACT_P3_CHANNEL_SIDECAR_H */
