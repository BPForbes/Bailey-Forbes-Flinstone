#ifndef NET_SERVER_H
#define NET_SERVER_H

#include "contract_p3_server.h"
#include "contract_p3_session_wire.h"
#include "contract_p3_sockets.h"
#include "contract_result.h"
#include "net_endpoint.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Server-side state (host dual-role: server + self-member)                  */
/*                                                                           */
/* `fl_net_server_t` is opaque to callers; the struct is exposed only so the */
/* shell command and the background task can stack-allocate or static-      */
/* allocate one instance without an extra heap dependency for foundations.  */
/* The single live host instance is what `cmd_server.c` references; tests   */
/* and net_server itself treat the struct as opaque otherwise.              */
/* ------------------------------------------------------------------------- */

typedef struct {
    fl_net_server_member_t members[FL_NET_SERVER_MAX_MEMBERS];
    fl_net_sock_handle_t listen_handle;
    fl_net_endpoint_t bind_ep;
    uint32_t bind_addr_be;
    uint16_t bind_port_host;
    fl_net_server_member_id_t next_member_id;
    uint8_t running; /* 1 between host_start and host_stop */
} fl_net_server_t;

/**
 * Initialise the global socket table if needed and zero `srv` so it can be
 * reused without leaking sockets from a prior session.
 */
fl_result_t fl_net_server_init(fl_net_server_t *srv);

/**
 * Bind and listen on `bind_addr_be:port_host` and register the host as the
 * first member with `host_principal`. Host is always **member_id 1**.
 *
 * On hosted builds without TCP sockets (`fl_net_sock_open` → NOSYS) this
 * returns `FL_RESULT_NOSYS` and `srv->running` stays 0.
 */
fl_result_t fl_net_server_host_start(fl_net_server_t *srv,
                                     uint32_t bind_addr_be, uint16_t port_host,
                                     const char *host_principal);

/** Bind/listen using a parsed endpoint (IPv4 or native IPv6 on hosted builds). */
fl_result_t fl_net_server_host_start_ep(fl_net_server_t *srv,
                                        const fl_net_endpoint_t *bind_ep,
                                        const char *host_principal);

/**
 * Broadcast `CTRL_KILL`, close every member socket and the listener, and
 * mark `srv` as not running. Idempotent.
 */
fl_result_t fl_net_server_host_stop(fl_net_server_t *srv);

/**
 * Accept at most one pending TCP connection (non-blocking). On success the
 * routine performs the HELLO/HELLO_ACK handshake, assigns a fresh
 * `member_id`, fills out `members[]`, emits **OP_JOIN_ANNOUNCE** to every
 * member (including self when `self_render` is non-NULL), and writes the
 * accepted display name to `*display_out` (truncated to
 * FL_NET_SERVER_DISPLAY_NAME_MAX bytes) when both pointers are non-NULL.
 *
 * Returns `FL_RESULT_TIMEDOUT` when no peer is waiting and the listener is
 * non-blocking; `FL_RESULT_OK` for one fully handshook peer; or an error
 * for malformed HELLO / capacity exhaustion.
 *
 * When a principal collision is detected the host also sends
 * **OP_NICK_PROMPT** to the new peer; the eventual nick reply is processed
 * by `fl_net_server_poll_members`.
 */
fl_result_t fl_net_server_accept_pending(fl_net_server_t *srv,
                                         char *display_out, size_t display_cap);

/**
 * Register an already-connected socket handle as a new server member and
 * complete the HELLO/HELLO_ACK handshake. Used by the reverse-dial NAT
 * bypass where the server dials out to a joining client and hands the
 * resulting connected socket directly to the session layer.
 */
fl_result_t fl_net_server_accept_handle(fl_net_server_t *srv,
                                        fl_net_sock_handle_t handle,
                                        char *display_out, size_t display_cap);

/**
 * Drain at most one inbound frame per member. Dispatches:
 *   - OP_MSG          → fan out OP_MSG_BROADCAST prefixed with sender display
 *   - OP_HOST_NICK_SET (from a client) → validate and apply, emit
 *                       OP_NICK_SET_ANNOUNCE to all
 *   - OP_CTRL_LEAVE  → drop the member, emit OP_LEAVE_ANNOUNCE
 *   - any EOF/error  → drop the member, emit OP_LEAVE_ANNOUNCE
 *
 * Returns `FL_RESULT_OK` after one pass. Off-host returns `FL_RESULT_NOSYS`.
 */
fl_result_t fl_net_server_poll_members(fl_net_server_t *srv);

/**
 * Host-only: broadcast a free-form `[Server Announcement] <text>` line to
 * every member. The host also prints to its own stdout via the colour
 * helper. Returns `FL_RESULT_INVAL` when `msg` is NULL/empty.
 */
fl_result_t fl_net_server_announce(fl_net_server_t *srv, const char *fmt, ...);

/**
 * Host-only: assign a global nick to `member_id`. Returns:
 *   OK     — nick applied, announcement broadcast, roster refreshed.
 *   INVAL  — `srv` NULL / not running, or `nick` fails nick_is_valid
 *            (empty, oversize, control bytes).
 *   NOENT  — no member has that `member_id`.
 *   BUSY   — nick collides with another connected member's principal
 *            or current nick (a member renaming to its own principal
 *            is a no-op, not a collision). On the wire side, a
 *            client-driven OP_HOST_NICK_SET that hits BUSY also gets
 *            an OP_ERR frame from the host.
 */
fl_result_t fl_net_server_set_host_nick(fl_net_server_t *srv,
                                        fl_net_server_member_id_t member_id,
                                        const char *nick);

/** Host-only: broadcast a public chat line as MSG_BROADCAST. Skips the
 *  host's own self-member; the host renders locally via fl_color_msg_*. */
fl_result_t fl_net_server_send_public(fl_net_server_t *srv, const char *text);

/** Host-only: deliver a private chat message to one recipient.
 *  NOENT when the recipient id is unknown or is the host itself. */
fl_result_t fl_net_server_send_private(fl_net_server_t *srv,
                                       fl_net_server_member_id_t recipient_id,
                                       const char *text);

/** Push an OP_MEMBER_LIST_SNAPSHOT to every peer. Called after join, leave,
 *  and nick changes so clients can render private-msg senders + connected. */
fl_result_t fl_net_server_broadcast_member_list(fl_net_server_t *srv);

/**
 * Host-only: pick the lowest non-host member as successor, broadcast
 * OP_CTRL_HOST_PROMOTE with [id, peer_ip_be, port], then close listener
 * and all member sockets. Returns OK + writes the successor into
 * `*new_host_out`, or NOENT when the host was the only member (server
 * is still stopped, no transfer frame is sent).
 */
fl_result_t fl_net_server_transfer_and_stop(fl_net_server_t *srv,
                                            fl_net_server_member_id_t *new_host_out);

/**
 * Render the display name for `member_id` into `out` (`out` must be at least
 * `FL_NET_SERVER_DISPLAY_NAME_MAX` bytes). Returns `FL_RESULT_OK` and writes
 * either `"Principal"`, `"Principal {N}"`, or `"Nick"` per the rules in
 * `contract_p3_server.h`.
 */
fl_result_t fl_net_server_member_display(const fl_net_server_t *srv,
                                         fl_net_server_member_id_t member_id,
                                         char *out, size_t cap);

/* Read access for the shell `server connected` verb. */
size_t fl_net_server_member_count(const fl_net_server_t *srv);
const fl_net_server_member_t *
fl_net_server_member_at(const fl_net_server_t *srv, size_t index);

/* ------------------------------------------------------------------------- */
/* Wire codec — exposed so net_client and tests share one path               */
/* ------------------------------------------------------------------------- */

/**
 * Serialise one session frame into `buf`. Returns the number of bytes
 * written (header + payload), or 0 on overflow / `payload_len` > max.
 */
size_t fl_net_session_encode_frame(uint8_t *buf, size_t cap, uint8_t opcode,
                                   const uint8_t *payload, uint16_t payload_len);

/**
 * Block-read one full session frame from `handle`. Returns FL_RESULT_OK
 * (sets `*opcode_out`, `*payload_len_out`, copies payload up to
 * `payload_cap`; on truncation `*payload_len_out` keeps the true wire
 * length so the caller can detect `plen > payload_cap`), FL_RESULT_EOF
 * on close, FL_RESULT_TIMEDOUT on timeout, or another rc on error.
 *
 * Not safe for non-blocking polling: a partial read at `timeout_ms == 0`
 * loses local-buffer bytes. Use `fl_net_session_recv_frame_nb` with a
 * per-stream `fl_net_session_rx_t` for poll loops; this variant is for
 * synchronous handshake paths (`fl_net_client_connect` waiting for
 * HELLO_ACK) where one window covers the full frame.
 */
fl_result_t fl_net_session_recv_frame(fl_net_sock_handle_t handle,
                                      uint8_t *opcode_out,
                                      uint8_t *payload_out, size_t payload_cap,
                                      uint16_t *payload_len_out,
                                      unsigned timeout_ms);

/**
 * Per-stream parser state for the non-blocking recv path. Holds any
 * in-progress frame so a `FL_RESULT_TIMEDOUT` mid-frame preserves
 * progress across poll iterations (TCP may split the 6-byte header or
 * the payload at any offset). One per remote stream; zero-initialise
 * or `fl_net_session_rx_reset()` before first use and on reconnect.
 */
typedef struct {
    uint8_t  hdr[FL_NET_SESSION_HDR_LEN];
    uint16_t hdr_filled;     /* 0..FL_NET_SESSION_HDR_LEN */
    uint16_t expected_plen;  /* parsed once hdr is complete */
    uint16_t body_filled;    /* 0..expected_plen */
    uint8_t  body[FL_NET_SESSION_MAX_MSG];
} fl_net_session_rx_t;

void fl_net_session_rx_reset(fl_net_session_rx_t *st);

/**
 * Non-blocking incremental recv. Accumulates bytes into `*st` across
 * calls. Returns FL_RESULT_OK when a full frame is assembled (outputs
 * filled, `*st` reset), FL_RESULT_TIMEDOUT when more bytes are needed,
 * FL_RESULT_EOF on close, FL_RESULT_INVAL on a malformed header (caller
 * drops the peer). Socket must be non-blocking (`fl_net_sock_set_nonblock`).
 */
fl_result_t fl_net_session_recv_frame_nb(fl_net_sock_handle_t handle,
                                          fl_net_session_rx_t *st,
                                          uint8_t *opcode_out,
                                          uint8_t *payload_out, size_t payload_cap,
                                          uint16_t *payload_len_out);

/**
 * Send one full session frame on `handle` (uses the encode helper internally).
 */
fl_result_t fl_net_session_send_frame(fl_net_sock_handle_t handle, uint8_t opcode,
                                      const uint8_t *payload, uint16_t payload_len);

/** Deliver one frame to a single connected member (excluding host self-slot). */
fl_result_t fl_net_server_send_to_member(fl_net_server_t *srv,
                                         fl_net_server_member_id_t to,
                                         uint8_t opcode,
                                         const uint8_t *payload,
                                         uint16_t plen);

/** Fan-out to every peer except `skip` (and the host self-member). */
fl_result_t fl_net_server_broadcast_except(fl_net_server_t *srv,
                                           fl_net_server_member_id_t skip,
                                           uint8_t opcode,
                                           const uint8_t *payload,
                                           uint16_t plen);

#endif /* NET_SERVER_H */
