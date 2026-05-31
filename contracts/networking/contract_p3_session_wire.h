/**
 * **P3-13 — Session wire vocabulary** (normative constants only).
 *
 * **Distribution:** one **TCP** byte stream carries length-prefixed **frames**.
 * Application payloads are UTF-8 unless a future **flags** bit says otherwise.
 * File-transfer frames reuse the same header; payload layout is in **P5-6**
 * (**contract_p5_file_delivery.h**) and **docs/SERVER.md**.
 *
 * **Implementation:** **net_server.c** / **cmd_server.c** are **not** part of the
 * network-prep train; this shard exists so contracts and docs stay aligned.
 */
#ifndef FL_CONTRACT_P3_SESSION_WIRE_H
#define FL_CONTRACT_P3_SESSION_WIRE_H

#include <stdint.h>

#define FL_CONTRACT_P3_13_SESSION_WIRE_CONTRACT_DEFINED 1

/** v1 magic octet (**'F'**). */
#define FL_NET_SESSION_MAGIC 0x46u
#define FL_NET_SESSION_VERSION 1u

/** Frame header size: magic, version, opcode, flags, length_be (2). */
#define FL_NET_SESSION_HDR_LEN 6u

#ifndef FL_NET_SESSION_MAX_MEMBERS
#define FL_NET_SESSION_MAX_MEMBERS 16u
#endif

#ifndef FL_NET_SESSION_MAX_MSG
#define FL_NET_SESSION_MAX_MSG 512u
#endif

#ifndef FL_NET_SESSION_INBOUND_RING
#define FL_NET_SESSION_INBOUND_RING 32u
#endif

/** Opcodes — chat control plane (v1). */
#define FL_NET_SESSION_OP_HELLO 0x01u
#define FL_NET_SESSION_OP_HELLO_ACK 0x02u
/** Host snapshot or reply: member list for server connected (ids + principals + host nick). */
#define FL_NET_SESSION_OP_MEMBER_LIST 0x03u
/** Host to member: principal collision; optional nickname requested (see docs/SERVER.md). */
#define FL_NET_SESSION_OP_NICK_PROMPT 0x04u
#define FL_NET_SESSION_OP_MSG 0x10u
#define FL_NET_SESSION_OP_MSG_BROADCAST 0x11u
#define FL_NET_SESSION_OP_CTRL_LEAVE 0x20u
#define FL_NET_SESSION_OP_CTRL_KILL 0x21u
#define FL_NET_SESSION_OP_CTRL_HOST_PROMOTE 0x22u
/** Host sets a host-global nickname for member_id (visible to all; target -user nick). */
#define FL_NET_SESSION_OP_HOST_NICK_SET 0x23u
#define FL_NET_SESSION_OP_FILE_OFFER   0x30u
#define FL_NET_SESSION_OP_FILE_CHUNK   0x31u
#define FL_NET_SESSION_OP_FILE_DONE    0x32u
#define FL_NET_SESSION_OP_FILE_ACCEPT  0x33u
#define FL_NET_SESSION_OP_FILE_DECLINE 0x34u
#define FL_NET_SESSION_OP_FILE_REVOKE  0x35u
#define FL_NET_SESSION_OP_FILE_LIST    0x36u
#define FL_NET_SESSION_OP_FILE_STATUS  0x37u
#define FL_NET_SESSION_OP_ERR 0x7Fu

_Static_assert(FL_NET_SESSION_HDR_LEN >= 6u, "session header must include length field");
_Static_assert(FL_NET_SESSION_MAX_MSG >= 64u, "session message cap unexpectedly small");

#endif /* FL_CONTRACT_P3_SESSION_WIRE_H */
