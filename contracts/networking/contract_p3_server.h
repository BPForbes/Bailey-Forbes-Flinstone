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
#define FL_CONTRACT_P3_13_SERVER_REV 1u

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

/**
 * Host → all members. Payload is one UTF-8 line: the **display name** of the
 * newly joined member, including the `{N}` disambiguator when applicable.
 * Examples: `"Flinstone"`, `"JohnDoe {2}"`.
 */
#define FL_NET_SESSION_OP_JOIN_ANNOUNCE 0x05u

/**
 * Host → all members. Payload is one UTF-8 line: the **display name** the
 * leaving member had at the moment of departure (post-nick if set).
 */
#define FL_NET_SESSION_OP_LEAVE_ANNOUNCE 0x06u

/**
 * Host → all members. Emitted whenever a host-global nick is set or changed
 * via **OP_HOST_NICK_SET** (host-driven) or accepted from **OP_NICK_PROMPT**.
 * Payload (UTF-8, single line):
 *     `User <old_display>. Their nickname is "<new_nick>".`
 * where `<old_display>` is the renderer output **before** the nick was applied.
 */
#define FL_NET_SESSION_OP_NICK_SET_ANNOUNCE 0x07u

/**
 * Host → all members. Free-form `server announce <message>` from the host.
 * Payload is one UTF-8 line up to `FL_NET_SERVER_ANNOUNCEMENT_MAX` bytes.
 * Rendered as `[Server Announcement] <payload>` in **BLU**.
 */
#define FL_NET_SESSION_OP_SERVER_ANNOUNCE 0x08u

/* ------------------------------------------------------------------------- */
/* In-memory types (host + client share these definitions)                   */
/* ------------------------------------------------------------------------- */

/** Member id assigned by the host at HELLO_ACK. `0` is reserved as "none". */
typedef uint16_t fl_net_server_member_id_t;
#define FL_NET_SERVER_MEMBER_ID_NONE ((fl_net_server_member_id_t)0)
#define FL_NET_SERVER_MEMBER_ID_HOST ((fl_net_server_member_id_t)1)

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
    FL_NET_SERVER_EVENT_CLOSED = 9
} fl_net_server_event_kind_t;

/** Client state machine (see docs/SERVER.md and #239 client/host model). */
typedef enum {
    FL_NET_CLIENT_STATE_DISCONNECTED = 0,
    FL_NET_CLIENT_STATE_CONNECTING = 1,
    FL_NET_CLIENT_STATE_CONNECTED = 2,
    FL_NET_CLIENT_STATE_DISCONNECTING = 3
} fl_net_client_state_t;

#endif /* FL_CONTRACT_P3_SERVER_H */
