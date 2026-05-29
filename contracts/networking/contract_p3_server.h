/**
 * **P3-13 — Server / client session model** (normative; foundations).
 *
 * **Distribution:** one **TCP** byte stream per peer carries length-prefixed
 * **session frames** (**contract_p3_session_wire.h**). This shard layers a
 * **member registry**, **nick map**, **disambiguation**, and **colour-coded
 * announcement protocol** on top of those frames.
 *
 * **Host dual role:** the node that runs **`server host`** is **simultaneously**
 * a server (owns the listener and the member registry, fans out broadcasts) and
 * a client (it appears in its own registry at **member_id 1** with the host's
 * principal, sees every broadcast through the same `on_event` hook, and uses
 * the same `server announce` / `server msg` send paths). The host's TX path
 * goes through **`fl_net_server_broadcast`**; the join client's TX path goes
 * through **`fl_net_client_send`**. Both routes carry identical wire frames.
 *
 * **Announcement palette** (rendered by the shell colour helpers in
 * **`userland/shell/fl_colors.h`**):
 *   - **BLU** `[Server Announcement] <text>` — join, leave, nick change, and
 *     any **`server announce`** message from the host.
 *   - **RED** `[ERROR] <text>` — session error frames (**OP_ERR**) and local
 *     authz / parse errors raised by the shell verbs.
 *   - **GRN** `[Server] <text>` — local success acknowledgements emitted by
 *     the shell verbs (not on the wire).
 *
 * **Display name rule:** when a single member has principal `P`, render `P`.
 * When two or more members share principal `P`, render each occurrence as
 * `P {N}` where **N** is the **disambig_index** assigned by the host in join
 * order (`1`-based across the duplicate set). A host-global **nick** assigned
 * via **OP_HOST_NICK_SET** **replaces** the rendered name with the nick alone
 * for everyone (`Jeff` instead of `JohnDoe {2}`).
 *
 * **Nick rules (host-global):**
 *   - A nick may not equal **any other connected member's principal** (a person
 *     cannot impersonate another real user). Comparison is case-sensitive.
 *   - A nick may not equal **any other connected member's existing nick**.
 *   - A member's nick **may** equal their own principal (no-op rename).
 *   - On collision the host returns **OP_ERR** to the requester.
 *
 * **Join → nick sequencing:** the host emits **OP_JOIN_ANNOUNCE** for a new
 * member **before** any **OP_NICK_SET_ANNOUNCE** that the same member triggers
 * via the **OP_NICK_PROMPT** reply. If the requested nick collides, the host
 * still emits the join announcement (using the disambiguated principal) and
 * then emits **OP_ERR** to the joining client only.
 *
 * **Implementation:** **kernel/core/net/net_server.[ch]**,
 * **kernel/core/net/net_client.[ch]**, **kernel/core/net/server_bg.[ch]**,
 * **userland/command/cmd_server.c**, **userland/shell/fl_colors.h**. This shard
 * does **not** define wire opcodes already named in **contract_p3_session_wire.h**;
 * it adds the **announcement** opcode family (server-side push events) and the
 * vocabulary the implementation files share.
 */
#ifndef FL_CONTRACT_P3_SERVER_H
#define FL_CONTRACT_P3_SERVER_H

#include <stdint.h>
#include <stddef.h>

#include "contract_p3_session_wire.h"
#include "contract_p3_sockets.h"

#define FL_CONTRACT_P3_13_SERVER_CONTRACT_DEFINED 1
#define FL_CONTRACT_P3_13_SERVER_REV 2u

/* ------------------------------------------------------------------------- */
/* Capacity / wire caps                                                      */
/* ------------------------------------------------------------------------- */

/** Max bytes of a principal username on the wire (NUL-terminated in C). */
#ifndef FL_NET_SERVER_PRINCIPAL_MAX
#define FL_NET_SERVER_PRINCIPAL_MAX 64u
#endif

/** Max bytes of a host-global nick (NUL-terminated in C). */
#ifndef FL_NET_SERVER_NICK_MAX
#define FL_NET_SERVER_NICK_MAX 32u
#endif

/** Max members in one session (host inclusive). Mirrors FL_NET_SESSION_MAX_MEMBERS. */
#ifndef FL_NET_SERVER_MAX_MEMBERS
#define FL_NET_SERVER_MAX_MEMBERS FL_NET_SESSION_MAX_MEMBERS
#endif

_Static_assert(FL_NET_SERVER_MAX_MEMBERS >= 2u,
               "server session must allow at least host + 1 peer");

/** Max bytes of one rendered announcement payload on the wire (UTF-8). */
#ifndef FL_NET_SERVER_ANNOUNCEMENT_MAX
#define FL_NET_SERVER_ANNOUNCEMENT_MAX 256u
#endif

_Static_assert(FL_NET_SERVER_ANNOUNCEMENT_MAX <= FL_NET_SESSION_MAX_MSG,
               "announcement must fit a session message");

/* ------------------------------------------------------------------------- */
/* Wire opcodes added by this shard                                          */
/*                                                                           */
/* contract_p3_session_wire.h reserves:                                      */
/*   0x01..0x04 (handshake/roster), 0x10..0x11 (msg/broadcast),              */
/*   0x20..0x23 (ctrl/host nick), 0x30..0x32 (file), 0x7F (err).             */
/* This shard claims the 0x05..0x0F handshake-adjacent range for             */
/* server-side push announcements.                                           */
/* ------------------------------------------------------------------------- */

/* Announce-family opcodes (host -> all). Each payload is a full UTF-8
 * line composed by the host; peer renderer prefixes
 * "[Server Announcement]: " and applies KBLU, no further formatting.
 *
 *   JOIN_ANNOUNCE       "<display> has joined."
 *   LEAVE_ANNOUNCE      "<display> has left."     (post-nick display)
 *   NICK_SET_ANNOUNCE   "User <old_display> has been nicked by host. "
 *                       "Their nickname is \"<new_nick>\"."
 *   SERVER_ANNOUNCE     free-form `server announce <text>` from the host;
 *                       fl_net_server_transfer_and_stop() also sends
 *                       "<new_host_display> is now the host" here.
 *
 * Payloads are bounded by FL_NET_SERVER_ANNOUNCEMENT_MAX. */
#define FL_NET_SESSION_OP_JOIN_ANNOUNCE      0x05u
#define FL_NET_SESSION_OP_LEAVE_ANNOUNCE     0x06u
#define FL_NET_SESSION_OP_NICK_SET_ANNOUNCE  0x07u
#define FL_NET_SESSION_OP_SERVER_ANNOUNCE    0x08u

/**
 * Client → host: request a direct (private) chat message to one recipient.
 * Payload:
 *   `[u16_be recipient_member_id][u16_be reserved (0)][utf8 text]`
 * Host validates the recipient is connected and routes a matching
 * **OP_MSG_DIRECT_DELIVER** frame to the recipient socket only. No global
 * fan-out; sender renders its own confirmation locally.
 */
#define FL_NET_SESSION_OP_MSG_DIRECT 0x12u

/**
 * Host → recipient: delivery of a private message. Payload:
 *   `[u16_be sender_member_id][u16_be reserved (0)][utf8 text]`
 * The recipient's client decodes this into an `EVENT_MSG_PRIVATE` carrying
 * the sender display name (resolved client-side from the prior MEMBER_LIST
 * snapshot and any local-only nicknames the recipient has set).
 */
#define FL_NET_SESSION_OP_MSG_DIRECT_DELIVER 0x13u

/**
 * Host → all members. Snapshot of the live member registry so each client
 * can resolve sender display names for private messages and the
 * `server connected` verb without round-tripping to the host. Payload:
 *   for each member: `[u16_be member_id][u8 is_host][u8 disambig_index]
 *                     [u8 principal_len][principal bytes]
 *                     [u8 nick_len]    [nick bytes]`
 * Frames smaller than the full roster are valid prefixes; the receiver
 * clears its cache and replays the body. Maximum payload is bounded by
 * `FL_NET_SESSION_MAX_MSG` and capped by `FL_NET_SERVER_MAX_MEMBERS`.
 */
#define FL_NET_SESSION_OP_MEMBER_LIST_SNAPSHOT 0x09u

/**
 * Host -> all at leave/transfer. Payload (IPv4 only today):
 *   [u16_be new_host_member_id][u32_be new_host_ip_be][u16_be new_host_port]
 * Recipient with `assigned_member_id == new_host_member_id` becomes the
 * new host (fl_net_server_host_start on its own IP + the supplied port);
 * other recipients reconnect to `new_host_ip:new_host_port`. When
 * `new_host_member_id == 0` there is no successor and recipients leave.
 *
 * IPv6 (#280): a sibling OP_CTRL_HOST_PROMOTE6 (0x24) carrying
 *   [u16 new_id][16 bytes ipv6_be][u16 port]
 * is the recommended forward shape so the v4 payload above stays byte-
 * stable across the dual-stack switchover.
 *
 * (0x22 was reserved by contract_p3_session_wire.h as
 * OP_CTRL_HOST_PROMOTE; this shard extends the payload encoding.)
 */

/* ------------------------------------------------------------------------- */
/* In-memory types (host + client share these definitions)                   */
/* ------------------------------------------------------------------------- */

/** Member id assigned by the host at HELLO_ACK. `0` is reserved as "none". */
typedef uint16_t fl_net_server_member_id_t;
#define FL_NET_SERVER_MEMBER_ID_NONE ((fl_net_server_member_id_t)0)
#define FL_NET_SERVER_MEMBER_ID_HOST ((fl_net_server_member_id_t)1)

/* Host is always member_id 1 by construction; pin in source so a future
 * slot-assignment refactor cannot silently break the stack-style reindex. */
_Static_assert(FL_NET_SERVER_MEMBER_ID_HOST == 1u,
               "host member_id is fixed at slot 1 by the dual-role model");
_Static_assert(FL_NET_SERVER_MEMBER_ID_NONE == 0u,
               "0 is the sentinel \"no member\" id");

/**
 * One member entry in the host's registry. The same struct mirrors on the
 * client side via the **OP_MEMBER_LIST** snapshot; `peer_handle` is **valid
 * only on the host side**.
 */
typedef struct {
    fl_net_server_member_id_t member_id;
    /** `1` when this slot is occupied. */
    uint8_t in_use;
    /** `1` when this member is the host (self). */
    uint8_t is_host;
    /**
     * Index within the duplicate-principal set, starting at `1`. `0` means
     * "no other member shares this principal; render without {N}".
     */
    uint8_t disambig_index;
    /** Reserved / pad. */
    uint8_t flags;
    /** UTF-8 principal (login name, NUL-terminated). */
    char principal[FL_NET_SERVER_PRINCIPAL_MAX];
    /** UTF-8 host-global nick (`""` when unset). */
    char nick[FL_NET_SERVER_NICK_MAX];
    /** Hosted socket handle for this member's TCP stream (host side only). */
    fl_net_sock_handle_t peer_handle;
    /** Network-byte-order IPv4 of the peer (host's accept side; 0 for self). */
    uint32_t peer_ip_be;
} fl_net_server_member_t;

/**
 * Display-name buffer guaranteed to fit:
 *   `Principal {NN}` ≤ FL_NET_SERVER_PRINCIPAL_MAX + 8 bytes for ` {NNN}` + NUL,
 *   or a `Nick` up to FL_NET_SERVER_NICK_MAX.
 */
#define FL_NET_SERVER_DISPLAY_NAME_MAX (FL_NET_SERVER_PRINCIPAL_MAX + 8u)

_Static_assert(FL_NET_SERVER_DISPLAY_NAME_MAX > FL_NET_SERVER_NICK_MAX,
               "display name buffer must fit the longer of {principal {N}} and nick");

/**
 * Event kinds delivered to a client-side `on_event` callback by
 * **`fl_net_client_poll`**. The shell renders these with the colour helpers.
 */
typedef enum {
    FL_NET_SERVER_EVENT_NONE = 0,
    /** Welcome + assigned member_id (after HELLO_ACK). */
    FL_NET_SERVER_EVENT_HELLO_ACK = 1,
    /** Host wants this client to send NICK_SET (principal collision). */
    FL_NET_SERVER_EVENT_NICK_PROMPT = 2,
    /** A chat message arrived (UTF-8). */
    FL_NET_SERVER_EVENT_MSG = 3,
    /** A join announcement arrived. */
    FL_NET_SERVER_EVENT_JOIN_ANNOUNCE = 4,
    /** A leave announcement arrived. */
    FL_NET_SERVER_EVENT_LEAVE_ANNOUNCE = 5,
    /** A nick-set announcement arrived. */
    FL_NET_SERVER_EVENT_NICK_SET_ANNOUNCE = 6,
    /** A free-form host announcement arrived (`server announce`). */
    FL_NET_SERVER_EVENT_SERVER_ANNOUNCE = 7,
    /** Host sent OP_ERR (payload is the error text). */
    FL_NET_SERVER_EVENT_ERR = 8,
    /** Host closed the session (CTRL_KILL or peer EOF). */
    FL_NET_SERVER_EVENT_CLOSED = 9,
    /** Direct (private) message arrived; `member_id` = sender id. */
    FL_NET_SERVER_EVENT_MSG_PRIVATE = 10,
    /** Host published an updated member-list snapshot. */
    FL_NET_SERVER_EVENT_MEMBER_LIST = 11,
    /** Host asked this client to take over (CTRL_HOST_PROMOTE). */
    FL_NET_SERVER_EVENT_HOST_PROMOTE = 12,
    /** Host asked a non-chosen client to reconnect to the new host. */
    FL_NET_SERVER_EVENT_HOST_REDIRECT = 13
} fl_net_server_event_kind_t;

/** Client state machine (see docs/SERVER.md and #239 client/host model). */
typedef enum {
    FL_NET_CLIENT_STATE_DISCONNECTED = 0,
    FL_NET_CLIENT_STATE_CONNECTING = 1,
    FL_NET_CLIENT_STATE_CONNECTED = 2,
    FL_NET_CLIENT_STATE_DISCONNECTING = 3
} fl_net_client_state_t;

#endif /* FL_CONTRACT_P3_SERVER_H */
