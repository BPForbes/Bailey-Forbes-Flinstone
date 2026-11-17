/* All multi-byte wire fields are big-endian; payloads are UTF-8 unless a flags bit says otherwise. */
#ifndef FL_CONTRACT_P3_SESSION_WIRE_H
#define FL_CONTRACT_P3_SESSION_WIRE_H

#include "contract_p5_file_delivery.h"

#include <stdint.h>

#define FL_CONTRACT_P3_13_SESSION_WIRE_CONTRACT_DEFINED 1

#define FL_NET_SESSION_MAGIC 0x46u
#define FL_NET_SESSION_VERSION 1u

/* Frame header: magic, version, opcode, flags, length_be (2). */
#define FL_NET_SESSION_HDR_LEN 6u

#define FL_NET_SERVER_DISCOVERY_PORT 7779u

#define FL_NET_DISCOVERY_MAGIC_0 0x46u
#define FL_NET_DISCOVERY_MAGIC_1 0x4Cu
#define FL_NET_DISCOVERY_MAGIC_2 0x42u
#define FL_NET_DISCOVERY_MAGIC_3 0x43u

#define FL_NET_DISCOVERY_OP_BEACON    0x01u  /* server → LAN broadcast */
#define FL_NET_DISCOVERY_OP_JOIN_REQ  0x02u  /* client → server: please dial back */

#ifndef FL_NET_SESSION_MAX_MEMBERS
#define FL_NET_SESSION_MAX_MEMBERS 16u
#endif

#ifndef FL_NET_SESSION_MAX_MSG
#define FL_NET_SESSION_MAX_MSG 512u
#endif

#ifndef FL_NET_SESSION_INBOUND_RING
#define FL_NET_SESSION_INBOUND_RING 32u
#endif

#define FL_NET_SESSION_OP_HELLO 0x01u
#define FL_NET_SESSION_OP_HELLO_ACK 0x02u
#define FL_NET_SESSION_OP_MEMBER_LIST 0x03u
#define FL_NET_SESSION_OP_NICK_PROMPT 0x04u
#define FL_NET_SESSION_OP_MSG 0x10u
#define FL_NET_SESSION_OP_MSG_BROADCAST 0x11u
#define FL_NET_SESSION_OP_MSG_DIRECT 0x12u
#define FL_NET_SESSION_OP_MSG_DIRECT_DELIVER 0x13u
#define FL_NET_SESSION_OP_MSG_META 0x14u

#define FL_NET_SESSION_OP_CTRL_LEAVE 0x20u
#define FL_NET_SESSION_OP_CTRL_KILL 0x21u
#define FL_NET_SESSION_OP_CTRL_HOST_PROMOTE 0x22u
#define FL_NET_SESSION_OP_HOST_NICK_SET 0x23u
#define FL_NET_SESSION_OP_CTRL_HOST_PROMOTE6 0x24u
#define FL_NET_SESSION_CTRL_HOST_PROMOTE_PAYLOAD_LEN  8u
#define FL_NET_SESSION_CTRL_HOST_PROMOTE6_PAYLOAD_LEN 20u

#define FL_NET_SESSION_OP_FILE_FIRST    0x30u
#define FL_NET_SESSION_OP_FILE_OFFER    0x30u
#define FL_NET_SESSION_OP_FILE_CHUNK    0x31u
#define FL_NET_SESSION_OP_FILE_DONE     0x32u
#define FL_NET_SESSION_OP_FILE_ACCEPT   0x33u
#define FL_NET_SESSION_OP_FILE_DECLINE  0x34u
#define FL_NET_SESSION_OP_FILE_REVOKE   0x35u
#define FL_NET_SESSION_OP_FILE_LIST     0x36u
#define FL_NET_SESSION_OP_FILE_STATUS   0x37u
#define FL_NET_SESSION_OP_FILE_META     0x38u
#define FL_NET_SESSION_OP_FILE_LAST     0x38u

/* The host verifies signatures against its own account registry; a member's self-asserted principal decides nothing. */
#define FL_NET_SESSION_OP_KQ_FIRST    0x39u
#define FL_NET_SESSION_OP_KQ_PART     0x39u /* letter bytes, either direction               */
#define FL_NET_SESSION_OP_KQ_OFFER    0x3Au /* sender -> host: public offer, letter length   */
#define FL_NET_SESSION_OP_KQ_ANNOUNCE 0x3Bu /* host -> recipient: co-sign request; empty
                                               recipient -> host: send my pending ones      */
#define FL_NET_SESSION_OP_KQ_COSIGN   0x3Cu /* recipient -> host: co-signature                */
#define FL_NET_SESSION_OP_KQ_RELEASE  0x3Du /* host -> recipient: letter length, then parts   */
#define FL_NET_SESSION_OP_KQ_ACK      0x3Eu /* recipient -> host: decoded (or refused)        */
#define FL_NET_SESSION_OP_KQ_KEYS     0x3Fu /* public keys from the host registry            */
#define FL_NET_SESSION_OP_KQ_LAST     0x3Fu

#define FL_NET_SESSION_OP_WIFI_FIRST       0x40u
#define FL_NET_SESSION_OP_WIFI_SAE_COMMIT  0x40u
#define FL_NET_SESSION_OP_WIFI_SAE_CONFIRM 0x41u
#define FL_NET_SESSION_OP_WIFI_EAPOL       0x42u
#define FL_NET_SESSION_OP_WIFI_ASSOC_REQ   0x43u
#define FL_NET_SESSION_OP_WIFI_ASSOC_RESP  0x44u
#define FL_NET_SESSION_OP_WIFI_AUTH_DONE   0x45u
#define FL_NET_SESSION_OP_WIFI_LAST        0x45u

/* 0x52 to 0x5F are reserved for the next range (FL-27's ledger frames). */
#define FL_NET_SESSION_OP_KQH_FIRST        0x50u
#define FL_NET_SESSION_OP_KQH_REQ          0x50u
#define FL_NET_SESSION_OP_KQH_DATA         0x51u
#define FL_NET_SESSION_OP_KQH_LAST         0x51u

#define FL_NET_SESSION_OP_KQR_FIRST        0x52u
#define FL_NET_SESSION_OP_KQR_REPORT       0x52u
#define FL_NET_SESSION_OP_KQR_LAST         0x52u
#define FL_KQ_REPORT_FRAME_MAX             480u

#define FL_NET_SESSION_OP_ERR 0x7Fu

#define FL_SERVER_FILE_STATUS_QUEUED 1u
#define FL_SERVER_FILE_STATUS_SENT 2u
#define FL_SERVER_FILE_STATUS_FAILED 3u

static inline int fl_net_session_is_file_opcode(uint8_t opcode)
{
    return opcode >= FL_NET_SESSION_OP_FILE_FIRST &&
           opcode <= FL_NET_SESSION_OP_FILE_LAST;
}

static inline int fl_net_session_is_kq_opcode(uint8_t opcode)
{
    return (opcode >= FL_NET_SESSION_OP_KQ_FIRST && opcode <= FL_NET_SESSION_OP_KQ_LAST) ||
           (opcode >= FL_NET_SESSION_OP_KQH_FIRST && opcode <= FL_NET_SESSION_OP_KQH_LAST) ||
           (opcode >= FL_NET_SESSION_OP_KQR_FIRST && opcode <= FL_NET_SESSION_OP_KQR_LAST);
}

static inline int fl_net_session_is_wifi_opcode(uint8_t opcode)
{
    return opcode >= FL_NET_SESSION_OP_WIFI_FIRST &&
           opcode <= FL_NET_SESSION_OP_WIFI_LAST;
}

fl_result_t fl_net_session_validate_file_frame(uint8_t opcode,
                                               uint8_t flags,
                                               uint16_t payload_len);

/* Public offers go to every other session, private offers to one target; the sender gets local status, not FILE_OFFER. */
fl_result_t fl_server_file_resolve_receiver(const char *target_name,
                                            uint16_t explicit_member_id,
                                            uint16_t sender_member_id,
                                            uint16_t *out_receiver_member_id);

fl_result_t fl_server_file_snapshot_display_names(uint16_t sender_member_id,
                                                  uint16_t receiver_member_id,
                                                  char *sender_display,
                                                  uint16_t sender_display_cap,
                                                  char *receiver_display,
                                                  uint16_t receiver_display_cap);

fl_result_t fl_server_file_render_offer_line(const fl_server_file_offer_t *offer,
                                             uint16_t viewer_member_id,
                                             char *out,
                                             uint16_t out_cap);

_Static_assert(FL_NET_SESSION_HDR_LEN >= 6u, "session header must include length field");
_Static_assert(FL_NET_SESSION_MAX_MSG >= 64u, "session message cap unexpectedly small");
_Static_assert(FL_NET_SESSION_OP_FILE_LAST < FL_NET_SESSION_OP_KQ_FIRST &&
                   FL_NET_SESSION_OP_KQ_LAST < FL_NET_SESSION_OP_WIFI_FIRST,
               "KeyQuorum opcodes sit between the file and wifi ranges");
_Static_assert(FL_NET_SESSION_OP_WIFI_LAST < FL_NET_SESSION_OP_KQH_FIRST &&
                   FL_NET_SESSION_OP_KQH_LAST < FL_NET_SESSION_OP_KQR_FIRST &&
                   FL_NET_SESSION_OP_KQR_LAST < FL_NET_SESSION_OP_ERR,
               "KeyQuorum history and report opcodes sit above wifi and below the error opcode");
_Static_assert(16u + 2u + FL_KQ_REPORT_FRAME_MAX <= FL_NET_SESSION_MAX_MSG,
               "KQR_REPORT header plus the largest report must fit one session message");
/* KQH_DATA total fits u32: the history bundle is bounded at 1 MiB. */
_Static_assert(16u + 4u + 4u + 480u <= FL_NET_SESSION_MAX_MSG,
               "KQH_DATA header plus 480 payload bytes must fit one session message");
_Static_assert((1u << 20) <= 0xFFFFFFFFu, "history bundle bound must fit the u32 total field");
_Static_assert(FL_NET_SESSION_OP_FILE_FIRST < FL_NET_SESSION_OP_FILE_LAST,
               "file opcode range must be contiguous");

#endif /* FL_CONTRACT_P3_SESSION_WIRE_H */
