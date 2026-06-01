#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include "contract_p3_server.h"
#include "contract_p3_sockets.h"
#include "contract_result.h"
#include "net_server.h" /* fl_net_session_rx_t for the embedded parser */

#include <stddef.h>
#include <stdint.h>

/*
 * P3-13 client (joining/participating side of a server session).
 *
 * The host runs net_server.c and inserts itself into its own member registry
 * at member_id 1. A client (joining peer) runs net_client.c: it owns the TCP
 * stream, parses inbound frames into events for the shell to render with the
 * fl_colors.h helpers, and exposes simple send / leave verbs that wrap the
 * wire codec.
 */

typedef struct fl_net_client_s fl_net_client_t;

/**
 * Event callback fired by `fl_net_client_poll` for each inbound frame. The
 * `data` pointer is whatever the caller passed to `_poll`. `text` is a
 * NUL-terminated copy of the payload (already-truncated to fit a stack
 * buffer); for HELLO_ACK, `member_id_out` is the assigned id parsed from the
 * payload prefix.
 */
typedef void (*fl_net_client_event_cb)(fl_net_server_event_kind_t kind,
                                       const char *text,
                                       fl_net_server_member_id_t member_id,
                                       void *data);

/* Cached snapshot of one remote member (parsed from
 * OP_MEMBER_LIST_SNAPSHOT). Used to render private-message senders and
 * `server connected` from the client side. Local-only nicks (set by the
 * viewer for themselves) live in `local_nick` and override the principal
 * display, but never override a host-global nick. */
typedef struct {
    fl_net_server_member_id_t member_id;
    uint8_t is_host;
    uint8_t disambig_index;
    char principal[FL_NET_SERVER_PRINCIPAL_MAX];
    char nick[FL_NET_SERVER_NICK_MAX];        /* host-global */
    char local_nick[FL_NET_SERVER_NICK_MAX];  /* client-local override */
} fl_net_client_member_t;

struct fl_net_client_s {
    fl_net_sock_handle_t peer_handle;
    fl_net_server_member_id_t assigned_member_id;
    fl_net_client_state_t state;
    char principal[FL_NET_SERVER_PRINCIPAL_MAX];
    char display_name[FL_NET_SERVER_DISPLAY_NAME_MAX];
    /* Own TCP source (network byte order) from getsockname() on connect;
     * the host-transfer path binds the new listener on this IP. */
    uint32_t local_ip_be;
    /* Cached OP_MEMBER_LIST_SNAPSHOT for sender resolution + connected. */
    fl_net_client_member_t members[FL_NET_SERVER_MAX_MEMBERS];
    size_t member_count;
    /* Non-blocking poll parser state; reset on init + every (re)connect. */
    fl_net_session_rx_t rx_state;
    /** Wire length of the last HOST_PROMOTE / HOST_PROMOTE6 payload delivered. */
    uint16_t last_host_promote_payload_len;
};

/**
 * Dispatch one already-received session frame: updates the cached roster
 * on OP_MEMBER_LIST_SNAPSHOT, parses the HELLO_ACK / MSG_DIRECT_DELIVER /
 * HOST_PROMOTE structured prefixes, then fires `cb`. Used by the `server
 * join` sync drain so non-NICK_PROMPT frames are preserved instead of
 * dropped. Returns the event kind, or NONE for opcodes the client ignores.
 */
fl_net_server_event_kind_t
fl_net_client_dispatch_frame(fl_net_client_t *client, uint8_t opcode,
                              const uint8_t *payload, uint16_t plen,
                              fl_net_client_event_cb cb, void *data);

/** Reset `client` to the disconnected zero state. */
fl_result_t fl_net_client_init(fl_net_client_t *client);

/**
 * Connect to `peer_be:port_host`, send OP_HELLO with `principal`, and block
 * until OP_HELLO_ACK arrives (or `timeout_ms` expires). On success the client
 * is in CONNECTED state and `display_name` / `assigned_member_id` are filled.
 */
fl_result_t fl_net_client_connect(fl_net_client_t *client,
                                  uint32_t peer_be, uint16_t port_host,
                                  const char *principal, unsigned timeout_ms);

/** Same as `fl_net_client_connect` but binds the local source address to
 * `local_be:0` before the TCP connect. Used by the multi-IP demo / tests
 * so each peer sources from a distinct loopback alias instead of 127.0.0.1.
 * Pass `local_be == 0` to behave like the plain connect. */
fl_result_t fl_net_client_connect_from(fl_net_client_t *client,
                                       uint32_t local_be,
                                       uint32_t peer_be, uint16_t port_host,
                                       const char *principal,
                                       unsigned timeout_ms);

/**
 * Send CTRL_LEAVE and close the socket. Idempotent.
 */
fl_result_t fl_net_client_disconnect(fl_net_client_t *client);

/**
 * Send one OP_MSG with `text` (UTF-8). The host will fan it out as
 * OP_MSG_BROADCAST prefixed with the client's display name.
 */
fl_result_t fl_net_client_send_msg(fl_net_client_t *client, const char *text);

/**
 * Send one OP_MSG_DIRECT (private chat) addressed to `recipient_id`. Host
 * routes the matching OP_MSG_DIRECT_DELIVER frame to exactly that recipient
 * (and to nobody else). Returns FL_RESULT_INVAL on bad args, FL_RESULT_NOENT
 * when there is no live member with that id in the local cache.
 */
fl_result_t fl_net_client_send_private(fl_net_client_t *client,
                                       fl_net_server_member_id_t recipient_id,
                                       const char *text);

/**
 * Look up the cached display name for `member_id`. Precedence (matches
 * `render_client_member_display` and `contract_p3_server.h`):
 *   1. host-global nick (`m->nick`) when set,
 *   2. otherwise local-only nick (`m->local_nick`) when set,
 *   3. otherwise `Principal {N}` (or just `Principal` when singular).
 * Returns FL_RESULT_NOENT when no cached member has that id.
 */
fl_result_t fl_net_client_member_display(const fl_net_client_t *client,
                                         fl_net_server_member_id_t member_id,
                                         char *out, size_t cap);

/**
 * Find a member by case-sensitive principal **or** any nick form. When the
 * principal is ambiguous (multiple connected members share it), require
 * `disambig_match >= 1` to pin to that ordinal; pass 0 to refuse ambiguity.
 * Returns the resolved member_id or FL_NET_SERVER_MEMBER_ID_NONE.
 */
fl_net_server_member_id_t
fl_net_client_member_lookup(const fl_net_client_t *client, const char *name,
                            unsigned disambig_match);

/** Read-only roster accessors for `server connected` from the client side. */
size_t fl_net_client_member_count(const fl_net_client_t *client);
const fl_net_client_member_t *
fl_net_client_member_at(const fl_net_client_t *client, size_t index);

/** Set a local-only nick for `member_id` (viewer-only; never goes on wire). */
fl_result_t fl_net_client_set_local_nick(fl_net_client_t *client,
                                         fl_net_server_member_id_t member_id,
                                         const char *local_nick);

/**
 * Request a host-global nick. Sends OP_HOST_NICK_SET; the host may return
 * OP_ERR on collision (rendered by the shell via the event callback).
 */
fl_result_t fl_net_client_set_nick(fl_net_client_t *client, const char *nick);

/**
 * Drain up to `max_frames` inbound frames non-blocking and emit events.
 * Returns the number of frames dispatched, or a negative `fl_result_t` on
 * error. On EOF the client transitions to DISCONNECTED and emits CLOSED.
 */
int fl_net_client_poll(fl_net_client_t *client, fl_net_client_event_cb cb,
                       void *data, unsigned max_frames);

fl_net_client_state_t fl_net_client_state(const fl_net_client_t *client);

#endif /* NET_CLIENT_H */
