#include "net_client.h"

#include "net_server.h" /* fl_net_session_*_frame, encode/recv helpers */
#include "net_socket.h"

#include <string.h>

fl_result_t fl_net_client_init(fl_net_client_t *client) {
    if (!client)
        return FL_RESULT_INVAL;
    memset(client, 0, sizeof(*client));
    client->peer_handle = FL_NET_SOCK_INVALID;
    client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
    return fl_net_sock_init();
}

fl_result_t fl_net_client_connect(fl_net_client_t *client,
                                  uint32_t peer_be, uint16_t port_host,
                                  const char *principal, unsigned timeout_ms) {
    fl_result_t rc;
    fl_net_sock_handle_t h = FL_NET_SOCK_INVALID;
    uint8_t opcode = 0;
    uint8_t payload[FL_NET_SESSION_MAX_MSG];
    uint16_t plen = 0;
    size_t prin_len;

    if (!client || !principal || !principal[0])
        return FL_RESULT_INVAL;
    if (client->state != FL_NET_CLIENT_STATE_DISCONNECTED)
        return FL_RESULT_BUSY;

    fl_net_client_init(client);

    rc = fl_net_sock_open(FL_NET_SOCK_TYPE_STREAM, &h);
    if (rc != FL_RESULT_OK)
        return rc;

    client->state = FL_NET_CLIENT_STATE_CONNECTING;
    rc = fl_net_sock_connect(h, peer_be, port_host);
    if (rc != FL_RESULT_OK) {
        fl_net_sock_close(h);
        client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
        return rc;
    }
    client->peer_handle = h;
    strncpy(client->principal, principal, sizeof(client->principal) - 1);

    /* HELLO with principal as the payload (no terminating NUL on wire). */
    prin_len = strnlen(client->principal, sizeof(client->principal));
    if (prin_len == 0u || prin_len > FL_NET_SESSION_MAX_MSG) {
        fl_net_sock_close(h);
        client->peer_handle = FL_NET_SOCK_INVALID;
        client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
        return FL_RESULT_INVAL;
    }
    rc = fl_net_session_send_frame(h, (uint8_t)FL_NET_SESSION_OP_HELLO,
                                   (const uint8_t *)client->principal,
                                   (uint16_t)prin_len);
    if (rc != FL_RESULT_OK) {
        fl_net_sock_close(h);
        client->peer_handle = FL_NET_SOCK_INVALID;
        client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
        return rc;
    }

    /* Wait for HELLO_ACK. */
    rc = fl_net_session_recv_frame(h, &opcode, payload, sizeof(payload), &plen,
                                   timeout_ms == 0u ? 2000u : timeout_ms);
    if (rc != FL_RESULT_OK || opcode != (uint8_t)FL_NET_SESSION_OP_HELLO_ACK ||
        plen < 2u) {
        fl_net_sock_close(h);
        client->peer_handle = FL_NET_SOCK_INVALID;
        client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
        return rc != FL_RESULT_OK ? rc : FL_RESULT_INVAL;
    }
    client->assigned_member_id =
        (fl_net_server_member_id_t)(((uint16_t)payload[0] << 8) | payload[1]);
    {
        size_t name_len = (size_t)plen - 2u;
        if (name_len >= sizeof(client->display_name))
            name_len = sizeof(client->display_name) - 1u;
        memcpy(client->display_name, payload + 2, name_len);
        client->display_name[name_len] = '\0';
    }
    /* Switch to non-blocking so the foreground shell stays responsive. */
    (void)fl_net_sock_set_nonblock(h, 1);
    client->state = FL_NET_CLIENT_STATE_CONNECTED;
    return FL_RESULT_OK;
}

fl_result_t fl_net_client_disconnect(fl_net_client_t *client) {
    if (!client)
        return FL_RESULT_INVAL;
    if (client->state == FL_NET_CLIENT_STATE_DISCONNECTED &&
        client->peer_handle == FL_NET_SOCK_INVALID)
        return FL_RESULT_OK;

    client->state = FL_NET_CLIENT_STATE_DISCONNECTING;
    if (client->peer_handle != FL_NET_SOCK_INVALID) {
        (void)fl_net_session_send_frame(client->peer_handle,
                                        (uint8_t)FL_NET_SESSION_OP_CTRL_LEAVE, NULL, 0u);
        fl_net_sock_close(client->peer_handle);
        client->peer_handle = FL_NET_SOCK_INVALID;
    }
    client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
    return FL_RESULT_OK;
}

fl_result_t fl_net_client_send_msg(fl_net_client_t *client, const char *text) {
    size_t n;
    if (!client || !text)
        return FL_RESULT_INVAL;
    if (client->state != FL_NET_CLIENT_STATE_CONNECTED ||
        client->peer_handle == FL_NET_SOCK_INVALID)
        return FL_RESULT_INVAL;
    n = strnlen(text, FL_NET_SESSION_MAX_MSG);
    if (n == 0u || n >= FL_NET_SESSION_MAX_MSG)
        return FL_RESULT_INVAL;
    return fl_net_session_send_frame(client->peer_handle,
                                     (uint8_t)FL_NET_SESSION_OP_MSG,
                                     (const uint8_t *)text, (uint16_t)n);
}

fl_result_t fl_net_client_set_nick(fl_net_client_t *client, const char *nick) {
    size_t n;
    if (!client || !nick)
        return FL_RESULT_INVAL;
    if (client->state != FL_NET_CLIENT_STATE_CONNECTED ||
        client->peer_handle == FL_NET_SOCK_INVALID)
        return FL_RESULT_INVAL;
    n = strnlen(nick, FL_NET_SERVER_NICK_MAX);
    if (n == 0u || n >= FL_NET_SERVER_NICK_MAX)
        return FL_RESULT_INVAL;
    return fl_net_session_send_frame(client->peer_handle,
                                     (uint8_t)FL_NET_SESSION_OP_HOST_NICK_SET,
                                     (const uint8_t *)nick, (uint16_t)n);
}

static fl_net_server_event_kind_t opcode_to_event(uint8_t opcode) {
    switch (opcode) {
    case FL_NET_SESSION_OP_HELLO_ACK:
        return FL_NET_SERVER_EVENT_HELLO_ACK;
    case FL_NET_SESSION_OP_NICK_PROMPT:
        return FL_NET_SERVER_EVENT_NICK_PROMPT;
    case FL_NET_SESSION_OP_MSG_BROADCAST:
        return FL_NET_SERVER_EVENT_MSG;
    case FL_NET_SESSION_OP_JOIN_ANNOUNCE:
        return FL_NET_SERVER_EVENT_JOIN_ANNOUNCE;
    case FL_NET_SESSION_OP_LEAVE_ANNOUNCE:
        return FL_NET_SERVER_EVENT_LEAVE_ANNOUNCE;
    case FL_NET_SESSION_OP_NICK_SET_ANNOUNCE:
        return FL_NET_SERVER_EVENT_NICK_SET_ANNOUNCE;
    case FL_NET_SESSION_OP_SERVER_ANNOUNCE:
        return FL_NET_SERVER_EVENT_SERVER_ANNOUNCE;
    case FL_NET_SESSION_OP_ERR:
        return FL_NET_SERVER_EVENT_ERR;
    case FL_NET_SESSION_OP_CTRL_KILL:
        return FL_NET_SERVER_EVENT_CLOSED;
    default:
        return FL_NET_SERVER_EVENT_NONE;
    }
}

int fl_net_client_poll(fl_net_client_t *client, fl_net_client_event_cb cb,
                       void *data, unsigned max_frames) {
    int dispatched = 0;

    if (!client)
        return (int)FL_RESULT_INVAL;
    if (client->state != FL_NET_CLIENT_STATE_CONNECTED)
        return 0;
    if (max_frames == 0u)
        max_frames = 16u;

    for (unsigned i = 0; i < max_frames; i++) {
        uint8_t opcode = 0;
        uint8_t payload[FL_NET_SESSION_MAX_MSG];
        uint16_t plen = 0;
        fl_result_t rc;
        char text[FL_NET_SESSION_MAX_MSG + 1];
        fl_net_server_event_kind_t kind;
        fl_net_server_member_id_t mid = FL_NET_SERVER_MEMBER_ID_NONE;
        size_t text_off = 0;
        size_t text_len;

        rc = fl_net_session_recv_frame(client->peer_handle, &opcode, payload,
                                       sizeof(payload), &plen, 0u);
        if (rc == FL_RESULT_TIMEDOUT)
            break;
        if (rc == FL_RESULT_EOF || rc != FL_RESULT_OK) {
            client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
            if (client->peer_handle != FL_NET_SOCK_INVALID) {
                fl_net_sock_close(client->peer_handle);
                client->peer_handle = FL_NET_SOCK_INVALID;
            }
            if (cb)
                cb(FL_NET_SERVER_EVENT_CLOSED, "", FL_NET_SERVER_MEMBER_ID_NONE, data);
            dispatched++;
            break;
        }

        kind = opcode_to_event(opcode);
        if (kind == FL_NET_SERVER_EVENT_NONE)
            continue;

        /* HELLO_ACK includes a 2-byte member_id prefix. */
        if (opcode == (uint8_t)FL_NET_SESSION_OP_HELLO_ACK && plen >= 2u) {
            mid = (fl_net_server_member_id_t)(((uint16_t)payload[0] << 8) | payload[1]);
            text_off = 2u;
        }

        text_len = plen > text_off ? (size_t)(plen - text_off) : 0u;
        if (text_len >= sizeof(text))
            text_len = sizeof(text) - 1u;
        if (text_len > 0u)
            memcpy(text, payload + text_off, text_len);
        text[text_len] = '\0';

        if (cb)
            cb(kind, text, mid, data);
        dispatched++;

        if (kind == FL_NET_SERVER_EVENT_CLOSED) {
            client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
            if (client->peer_handle != FL_NET_SOCK_INVALID) {
                fl_net_sock_close(client->peer_handle);
                client->peer_handle = FL_NET_SOCK_INVALID;
            }
            break;
        }
    }
    return dispatched;
}

fl_net_client_state_t fl_net_client_state(const fl_net_client_t *client) {
    return client ? client->state : FL_NET_CLIENT_STATE_DISCONNECTED;
}
