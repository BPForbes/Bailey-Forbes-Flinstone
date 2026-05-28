#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include "contract_p3_server.h"
#include "contract_p3_sockets.h"
#include "contract_result.h"

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

struct fl_net_client_s {
    fl_net_sock_handle_t peer_handle;
    fl_net_server_member_id_t assigned_member_id;
    fl_net_client_state_t state;
    char principal[FL_NET_SERVER_PRINCIPAL_MAX];
    char display_name[FL_NET_SERVER_DISPLAY_NAME_MAX];
};

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
