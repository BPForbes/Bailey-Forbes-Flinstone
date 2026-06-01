/**
 * **P3 / P5 — 5-byte channel packet metadata** (normative wire vocabulary).
 *
 * Bytes form a 40-bit big-endian word: **Byte 0 is MSB** and is scanned first by the
 * session layer. FILE_OFFER and future MSG payloads prepend these five octets before
 * the length-prefixed body. Encoders must write **MSB-first** per-byte flag layout so
 * fast-path gates can test `buf[n] & 0x80u` without shifting.
 *
 * **Implementation:** **kernel/core/net/net_pkt_channel_meta.c**
 * (**net_pkt_channel_meta.h**). FILE_OFFER encode/decode: **net_file_delivery.c**.
 *
 * Design: GitHub issue **#297**.
 */
#ifndef FL_CONTRACT_PKT_CHANNEL_META_H
#define FL_CONTRACT_PKT_CHANNEL_META_H

#include <stddef.h>
#include <stdint.h>

#define FL_CONTRACT_PKT_CHANNEL_META_DEFINED 1

/** Metadata prefix length for channel payloads (file + message). */
#define FL_PKT_CHANNEL_META_LEN 5u

/* ── Byte 0 — General Channel (bit 39..32 of the 40-bit word) ─────────── */

#define FL_PKT_VALID          (1u << 7) /* 0 = discard (file revoke / expired) */
#define FL_PKT_IS_HOST        (1u << 6)
#define FL_PKT_IS_SENDER      (1u << 5) /* sender local echo — suppress re-display */
#define FL_PKT_DESTINATION    (1u << 4) /* final hop / delivery confirmation */
#define FL_PKT_PRIVATE        (1u << 3)
#define FL_PKT_TYPE           (1u << 2) /* 0 = message · 1 = file */
#define FL_PKT_ID_BASED       (1u << 1)
#define FL_PKT_NICK_BASED     (1u << 0)

/* ── Byte 1 — File Permissions (maps fl_file_perms_t; excludes reserved/SFTP) ─ */

#define FL_PKT_FILE_EXPIRATION (1u << 7) /* FL_FILE_FLAG_EXPIRES */
#define FL_PKT_FILE_VIEW       (1u << 6) /* FL_FILE_PERM_VIEW */
#define FL_PKT_FILE_EDIT       (1u << 5) /* FL_FILE_PERM_EDIT */
#define FL_PKT_FILE_OVERWRITE  (1u << 4) /* FL_FILE_PERM_OVERWRITE */
#define FL_PKT_FILE_WRITE      (1u << 3) /* FL_FILE_PERM_WRITE */
#define FL_PKT_FILE_RUN        (1u << 2) /* FL_FILE_PERM_RUN */
#define FL_PKT_FILE_CREATE     (1u << 1) /* FL_FILE_PERM_CREATE */
#define FL_PKT_FILE_PUBLIC     (1u << 0) /* FL_FILE_FLAG_PUBLIC */

/* ── Byte 2 — File State (FL_FILE_FLAG_REVOKED lives only in Byte 0 Valid) ─── */

#define FL_PKT_FILE_LOCKED     (1u << 7) /* FL_FILE_FLAG_LOCKED */
#define FL_PKT_FILE_HAS_META   (1u << 6) /* supplementary metadata follows payload */
#define FL_PKT_FILE_DIR_MAKE   (1u << 5) /* create destination directory on delivery */
#define FL_PKT_FILE_SVR_SHARE  (1u << 4) /* FL_FILE_PERM_SERVER_SHARE */
#define FL_PKT_FILE_SCAN_REQ   (1u << 3) /* FL_FILE_FLAG_SCAN_REQUIRED */
#define FL_PKT_FILE_DL_ALLOW   (1u << 2) /* FL_FILE_PERM_DOWNLOAD */
#define FL_PKT_FILE_ACCEPT     (1u << 1) /* FL_FILE_PERM_ACCEPT */
/* bit 0 reserved */

/* ── Byte 3 — Message Content Flags ─────────────────────────────────────── */

#define FL_PKT_MSG_BINARY      (1u << 7)
#define FL_PKT_MSG_SYSTEM      (1u << 6)
#define FL_PKT_MSG_EDITED      (1u << 5)
#define FL_PKT_MSG_REPLY       (1u << 4)
#define FL_PKT_MSG_TRUNCATED   (1u << 3)
#define FL_PKT_MSG_ENCRYPTED   (1u << 2)
/* bits 1..0 reserved */

/* ── Byte 4 — Message Delivery ─────────────────────────────────────────── */

#define FL_PKT_MSG_ACKNOWLEDGED (1u << 7)
#define FL_PKT_MSG_READ         (1u << 6)
#define FL_PKT_MSG_RETAINED     (1u << 5)
#define FL_PKT_MSG_NICK_SNAP    (1u << 4)
#define FL_PKT_MSG_RATE_LIM     (1u << 3)
#define FL_PKT_MSG_TTL_ACTIVE   (1u << 2)
/* bits 1..0 reserved */

/** FILE_OFFER body starts after the 5-byte metadata prefix. */
#define FL_FILE_OFFER_WIRE_BODY_OFFSET FL_PKT_CHANNEL_META_LEN

/**
 * Byte offset of the big-endian **file_perms** u16 inside a FILE_OFFER body
 * (after metadata + sender/receiver member ids).
 */
#define FL_FILE_OFFER_WIRE_FILE_PERMS_OFFSET \
    (FL_FILE_OFFER_WIRE_BODY_OFFSET + 4u)

#define FL_FILE_OFFER_WIRE_MIN_LEN (FL_FILE_OFFER_WIRE_FILE_PERMS_OFFSET + 2u)

/** Pre-metadata layout (legacy); kept for minimum-length guards on old captures. */
#define FL_FILE_OFFER_WIRE_LEGACY_FILE_PERMS_OFFSET 4u
#define FL_FILE_OFFER_WIRE_LEGACY_MIN_LEN 5u

/** True when **payload** is long enough to carry the 5-byte metadata prefix. */
static inline int fl_pkt_wire_has_meta(const uint8_t *payload, uint16_t payload_len)
{
    return payload != NULL && payload_len >= FL_PKT_CHANNEL_META_LEN;
}

/**
 * True when **payload** uses the metadata-prefixed FILE_OFFER layout (not legacy).
 * Requires the explicit **FL_PKT_TYPE** marker in byte 0, not length alone.
 */
static inline int fl_pkt_file_offer_has_meta_layout(const uint8_t *payload,
                                                    uint16_t payload_len)
{
    return payload != NULL && payload_len >= FL_PKT_CHANNEL_META_LEN &&
           (payload[0] & FL_PKT_TYPE) != 0;
}

/** Unified pre-dispatch gate: Byte 0 bit 7 (MSB) must be set. */
static inline int fl_pkt_wire_valid(const uint8_t *payload, uint16_t payload_len)
{
    if (!fl_pkt_wire_has_meta(payload, payload_len))
        return 0;
    return (payload[0] & FL_PKT_VALID) != 0;
}

/** File vs message branch: Byte 0 **FL_PKT_TYPE**. */
static inline int fl_pkt_wire_is_file(const uint8_t *payload, uint16_t payload_len)
{
    if (!fl_pkt_wire_has_meta(payload, payload_len))
        return 0;
    return (payload[0] & FL_PKT_TYPE) != 0;
}

/**
 * Revoke / invalid gate for FILE_OFFER (and future MSG TTL) payloads.
 * Prefers Byte 0 **FL_PKT_VALID**; falls back to legacy MSB scan at offset 4 when
 * the buffer is too short to carry metadata.
 */
static inline int fl_file_offer_wire_payload_revoked(const uint8_t *payload,
                                                     uint16_t payload_len)
{
    if (!payload)
        return 0;
    if (fl_pkt_file_offer_has_meta_layout(payload, payload_len))
        return !fl_pkt_wire_valid(payload, payload_len);
    if (payload_len < FL_FILE_OFFER_WIRE_LEGACY_MIN_LEN)
        return 0;
    return (payload[FL_FILE_OFFER_WIRE_LEGACY_FILE_PERMS_OFFSET] & 0x80u) != 0;
}

#endif /* FL_CONTRACT_PKT_CHANNEL_META_H */
