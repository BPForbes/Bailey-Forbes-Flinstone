/*
 * net_client.c — joining-peer side of the P3-13 session protocol.
 *
 * Owns one TCP stream to the host, the assigned member_id, a cached
 * snapshot of the roster (rebuilt from each OP_MEMBER_LIST_SNAPSHOT),
 * and the partial-frame parser state used by the non-blocking poller.
 * The host side lives in net_server.c; both share the wire codec from
 * net_server.h so encode/decode logic stays in one place.
 */
#include "net_client.h"
#include "net_file_delivery.h"
#include "net_file_delivery.h"

#include "contract_p3_packet.h"
#include "net_server.h" /* fl_net_session_*_frame, encode/recv helpers */
#include "net_socket.h"

#include <stdio.h>
#include <string.h>

fl_result_t fl_net_client_init(fl_net_client_t *client) {
    if (!client)
        return FL_RESULT_INVAL;
    memset(client, 0, sizeof(*client));
    client->peer_handle = FL_NET_SOCK_INVALID;
    client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
    fl_net_session_rx_reset(&client->rx_state);
    return fl_net_sock_init();
}

/*
 * Open a TCP socket (optionally bound to `local_be` first), connect to
 * `peer_be:port_host`, send our HELLO with the principal, then block up
 * to `timeout_ms` for HELLO_ACK so the caller can return synchronously
 * with `assigned_member_id` and `display_name` already populated.
 *
 * The synchronous HELLO_ACK wait uses the blocking codec (not the nb
 * one) because there is only one window's worth of bytes to read here
 * and a partial read across that window is impossible — recv with a
 * positive timeout either fills the buffer or times out cleanly.
 */
static fl_result_t client_connect_ep_impl(fl_net_client_t *client,
                                          const fl_net_endpoint_t *local,
                                          const fl_net_endpoint_t *peer,
                                          const char *principal,
                                          unsigned timeout_ms) {
    fl_result_t rc;
    fl_net_sock_handle_t h = FL_NET_SOCK_INVALID;
    uint8_t opcode = 0;
    uint8_t payload[FL_NET_SESSION_MAX_MSG];
    uint16_t plen = 0;
    size_t prin_len;
    fl_net_endpoint_t local_ep;

    if (!client || !peer || !principal || !principal[0])
        return FL_RESULT_INVAL;
    if (peer->family != FL_NET_ADDR_FAMILY_V4 &&
        peer->family != FL_NET_ADDR_FAMILY_V6)
        return FL_RESULT_INVAL;
    if (client->state != FL_NET_CLIENT_STATE_DISCONNECTED)
        return FL_RESULT_BUSY;

    prin_len = strnlen(principal, sizeof(client->principal));
    if (prin_len == 0u ||
        prin_len == sizeof(client->principal) ||
        prin_len > (size_t)FL_NET_SESSION_MAX_MSG)
        return FL_RESULT_INVAL;

    rc = fl_net_client_init(client);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = fl_net_sock_open_for(peer, FL_NET_SOCK_TYPE_STREAM, &h);
    if (rc != FL_RESULT_OK)
        return rc;

    client->state = FL_NET_CLIENT_STATE_CONNECTING;
    if (local && local->family != 0u)
        rc = fl_net_sock_connect_from_ep(h, local, peer);
    else
        rc = fl_net_sock_connect_from_ep(h, NULL, peer);
    if (rc != FL_RESULT_OK) {
        fl_net_sock_close(h);
        client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
        return rc;
    }
    client->peer_handle = h;
    memcpy(client->principal, principal, prin_len);
    client->principal[prin_len] = '\0';
    rc = fl_net_session_send_frame(h, (uint8_t)FL_NET_SESSION_OP_HELLO,
                                   (const uint8_t *)client->principal,
                                   (uint16_t)prin_len);
    if (rc != FL_RESULT_OK) {
        fl_net_sock_close(h);
        client->peer_handle = FL_NET_SOCK_INVALID;
        client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
        return rc;
    }

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
    memset(&local_ep, 0, sizeof(local_ep));
    if (fl_net_sock_local_endpoint(h, &local_ep) == FL_RESULT_OK) {
        client->local_ep = local_ep;
        (void)fl_net_endpoint_to_v4(&local_ep, &client->local_ip_be);
    } else {
        (void)fl_net_sock_local_ipv4(h, &client->local_ip_be);
        fl_net_endpoint_from_v4(client->local_ip_be, 0u, &client->local_ep);
    }
    (void)fl_net_sock_set_nonblock(h, 1);
    fl_net_session_rx_reset(&client->rx_state);
    client->state = FL_NET_CLIENT_STATE_CONNECTED;
    return FL_RESULT_OK;
}

static fl_result_t client_connect_impl(fl_net_client_t *client,
                                       uint32_t local_be,
                                       uint32_t peer_be, uint16_t port_host,
                                       const char *principal,
                                       unsigned timeout_ms) {
    fl_net_endpoint_t local;
    fl_net_endpoint_t peer;

    fl_net_endpoint_from_v4(peer_be, port_host, &peer);
    if (local_be != 0u)
        fl_net_endpoint_from_v4(local_be, 0u, &local);
    else
        memset(&local, 0, sizeof(local));
    return client_connect_ep_impl(client, local_be != 0u ? &local : NULL, &peer,
                                  principal, timeout_ms);
}

fl_result_t fl_net_client_connect(fl_net_client_t *client, uint32_t peer_be,
                                  uint16_t port_host, const char *principal,
                                  unsigned timeout_ms) {
    return client_connect_impl(client, 0u, peer_be, port_host, principal, timeout_ms);
}

fl_result_t fl_net_client_connect_from(fl_net_client_t *client,
                                       uint32_t local_be, uint32_t peer_be,
                                       uint16_t port_host, const char *principal,
                                       unsigned timeout_ms) {
    return client_connect_impl(client, local_be, peer_be, port_host, principal,
                                timeout_ms);
}

fl_result_t fl_net_client_connect_ep(fl_net_client_t *client,
                                     const fl_net_endpoint_t *local,
                                     const fl_net_endpoint_t *peer,
                                     const char *principal,
                                     unsigned timeout_ms) {
    return client_connect_ep_impl(client, local, peer, principal, timeout_ms);
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

static uint32_t s_client_msg_serial;

fl_result_t fl_net_client_send_msg(fl_net_client_t *client, const char *text) {
    size_t n;
    char transfer_id[FL_SERVER_SHARE_ID_MAX];
    fl_result_t rc;

    if (!client || !text)
        return FL_RESULT_INVAL;
    if (client->state != FL_NET_CLIENT_STATE_CONNECTED ||
        client->peer_handle == FL_NET_SOCK_INVALID)
        return FL_RESULT_INVAL;
    /* Peek one past MAX_MSG so an exactly-fit text is accepted. */
    n = strnlen(text, (size_t)FL_NET_SESSION_MAX_MSG + 1u);
    if (n == 0u || n > (size_t)FL_NET_SESSION_MAX_MSG)
        return FL_RESULT_INVAL;
    s_client_msg_serial++;
    snprintf(transfer_id, sizeof(transfer_id), "msg-%u-%u",
             (unsigned)client->assigned_member_id, s_client_msg_serial);
    (void)fl_net_msg_send_meta_broadcast(client, transfer_id, "broadcast", 0u,
                                         FL_FILE_FLAG_PUBLIC);
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
    /* Host-side storage is char[NICK_MAX] (C string) so the upper bound
     * stays `>= NICK_MAX` to leave room for the NUL. */
    n = strnlen(nick, (size_t)FL_NET_SERVER_NICK_MAX);
    if (n == 0u || n >= (size_t)FL_NET_SERVER_NICK_MAX)
        return FL_RESULT_INVAL;
    return fl_net_session_send_frame(client->peer_handle,
                                     (uint8_t)FL_NET_SESSION_OP_HOST_NICK_SET,
                                     (const uint8_t *)nick, (uint16_t)n);
}

/* Map a wire opcode to the externally-visible event the shell renders.
 * Unknown / internal opcodes (e.g. handshake echoes) collapse to NONE so
 * the poll loop skips them without spending a callback round trip. */
static fl_net_server_event_kind_t opcode_to_event(uint8_t opcode) {
    switch (opcode) {
    case FL_NET_SESSION_OP_HELLO_ACK:
        return FL_NET_SERVER_EVENT_HELLO_ACK;
    case FL_NET_SESSION_OP_NICK_PROMPT:
        return FL_NET_SERVER_EVENT_NICK_PROMPT;
    case FL_NET_SESSION_OP_MSG_BROADCAST:
        return FL_NET_SERVER_EVENT_MSG;
    case FL_NET_SESSION_OP_MSG_DIRECT_DELIVER:
        return FL_NET_SERVER_EVENT_MSG_PRIVATE;
    case FL_NET_SESSION_OP_JOIN_ANNOUNCE:
        return FL_NET_SERVER_EVENT_JOIN_ANNOUNCE;
    case FL_NET_SESSION_OP_LEAVE_ANNOUNCE:
        return FL_NET_SERVER_EVENT_LEAVE_ANNOUNCE;
    case FL_NET_SESSION_OP_NICK_SET_ANNOUNCE:
        return FL_NET_SERVER_EVENT_NICK_SET_ANNOUNCE;
    case FL_NET_SESSION_OP_SERVER_ANNOUNCE:
        return FL_NET_SERVER_EVENT_SERVER_ANNOUNCE;
    case FL_NET_SESSION_OP_MEMBER_LIST_SNAPSHOT:
        return FL_NET_SERVER_EVENT_MEMBER_LIST;
    case FL_NET_SESSION_OP_CTRL_HOST_PROMOTE:
        return FL_NET_SERVER_EVENT_HOST_PROMOTE;
    case FL_NET_SESSION_OP_ERR:
        return FL_NET_SERVER_EVENT_ERR;
    case FL_NET_SESSION_OP_CTRL_KILL:
        return FL_NET_SERVER_EVENT_CLOSED;
    case FL_NET_SESSION_OP_FILE_OFFER:
        return FL_NET_SERVER_EVENT_FILE_OFFER;
    default:
        return FL_NET_SERVER_EVENT_NONE;
    }
}

/* Parse OP_MEMBER_LIST_SNAPSHOT into the cached roster.
 *
 * Local-only nicks (a viewer's private label for some other member) are
 * NOT carried on the wire — only the host-global nick is. To avoid
 * losing them every time the host re-broadcasts the snapshot, we copy
 * any local_nicks set on the OLD cache aside, rebuild the cache from
 * the wire, then re-apply by member_id. Members who disconnect between
 * snapshots silently drop their local_nick along with the slot, which
 * is the right behaviour. */
static void client_decode_member_list(fl_net_client_t *client,
                                      const uint8_t *payload, size_t plen) {
    size_t off = 0;
    size_t out = 0;
    char saved_local_nicks[FL_NET_SERVER_MAX_MEMBERS][FL_NET_SERVER_NICK_MAX];
    fl_net_server_member_id_t saved_ids[FL_NET_SERVER_MAX_MEMBERS];
    size_t saved_n = 0;
    for (size_t i = 0; i < client->member_count && saved_n < FL_NET_SERVER_MAX_MEMBERS; i++) {
        if (client->members[i].local_nick[0] != '\0') {
            memcpy(saved_local_nicks[saved_n], client->members[i].local_nick,
                   FL_NET_SERVER_NICK_MAX);
            saved_ids[saved_n] = client->members[i].member_id;
            saved_n++;
        }
    }

    memset(client->members, 0, sizeof(client->members));
    client->member_count = 0;

    while (off + 6u <= plen && out < FL_NET_SERVER_MAX_MEMBERS) {
        fl_net_client_member_t *m = &client->members[out];
        uint8_t plen_b, nlen_b;
        m->member_id = (fl_net_server_member_id_t)
            (((uint16_t)payload[off] << 8) | payload[off + 1]);
        m->is_host = payload[off + 2];
        m->disambig_index = payload[off + 3];
        plen_b = payload[off + 4];
        off += 5;
        if (off + plen_b + 1u > plen)
            break;
        {
            size_t cp = plen_b < sizeof(m->principal) - 1u ? plen_b
                                                            : sizeof(m->principal) - 1u;
            memcpy(m->principal, payload + off, cp);
            m->principal[cp] = '\0';
        }
        off += plen_b;
        nlen_b = payload[off];
        off += 1;
        if (off + nlen_b > plen)
            break;
        if (nlen_b > 0u) {
            size_t cn = nlen_b < sizeof(m->nick) - 1u ? nlen_b
                                                       : sizeof(m->nick) - 1u;
            memcpy(m->nick, payload + off, cn);
            m->nick[cn] = '\0';
        }
        off += nlen_b;

        /* Re-apply any local nick the viewer had for this id. */
        for (size_t s = 0; s < saved_n; s++) {
            if (saved_ids[s] == m->member_id) {
                memcpy(m->local_nick, saved_local_nicks[s], FL_NET_SERVER_NICK_MAX);
                break;
            }
        }
        out++;
    }
    client->member_count = out;
}

static const fl_net_client_member_t *
client_member_const(const fl_net_client_t *client, fl_net_server_member_id_t id) {
    if (!client)
        return NULL;
    for (size_t i = 0; i < client->member_count; i++) {
        if (client->members[i].member_id == id)
            return &client->members[i];
    }
    return NULL;
}

/* Render precedence (display-name rule, contract_p3_server.h):
 *   host-global nick  >  client-local nick  >  Principal {N}  >  Principal
 *
 * The host-global override means a viewer who set a local nick "Sis" for
 * member 3 still sees "Boss" if the host later assigns that global nick,
 * matching the user-facing rule that the host's view is authoritative. */
static void render_client_member_display(const fl_net_client_member_t *m,
                                         char *out, size_t cap) {
    if (!out || cap == 0u)
        return;
    out[0] = '\0';
    if (!m)
        return;
    if (m->nick[0] != '\0') {
        snprintf(out, cap, "%s", m->nick);
        return;
    }
    if (m->local_nick[0] != '\0') {
        snprintf(out, cap, "%s", m->local_nick);
        return;
    }
    if (m->disambig_index == 0u)
        snprintf(out, cap, "%s", m->principal);
    else
        snprintf(out, cap, "%s {%u}", m->principal, (unsigned)m->disambig_index);
}

fl_result_t fl_net_client_member_display(const fl_net_client_t *client,
                                         fl_net_server_member_id_t member_id,
                                         char *out, size_t cap) {
    const fl_net_client_member_t *m = client_member_const(client, member_id);
    if (!out || cap == 0u)
        return FL_RESULT_INVAL;
    if (!m) {
        out[0] = '\0';
        return FL_RESULT_NOENT;
    }
    render_client_member_display(m, out, cap);
    return FL_RESULT_OK;
}

/* Two-pass lookup so a nick (which is by construction unique) short-
 * circuits before we scan principals. The second pass refuses to return
 * an ambiguous principal match unless `disambig_match` pins the `{N}`
 * ordinal; this is how `server msg -user X` errors out with a clear
 * message instead of silently delivering to a random duplicate. */
fl_net_server_member_id_t
fl_net_client_member_lookup(const fl_net_client_t *client, const char *name,
                            unsigned disambig_match) {
    fl_net_server_member_id_t hit = FL_NET_SERVER_MEMBER_ID_NONE;
    unsigned principal_count = 0;
    if (!client || !name || !name[0])
        return FL_NET_SERVER_MEMBER_ID_NONE;
    for (size_t i = 0; i < client->member_count; i++) {
        const fl_net_client_member_t *m = &client->members[i];
        if (m->nick[0] && strncmp(m->nick, name, FL_NET_SERVER_NICK_MAX) == 0)
            return m->member_id;
        if (m->local_nick[0] && strncmp(m->local_nick, name, FL_NET_SERVER_NICK_MAX) == 0)
            return m->member_id;
    }
    /* Second pass: principal match (count for ambiguity). */
    for (size_t i = 0; i < client->member_count; i++) {
        const fl_net_client_member_t *m = &client->members[i];
        if (strncmp(m->principal, name, FL_NET_SERVER_PRINCIPAL_MAX) == 0) {
            principal_count++;
            if (disambig_match == 0u) {
                hit = m->member_id;
            } else if (m->disambig_index == (uint8_t)disambig_match) {
                return m->member_id;
            }
        }
    }
    if (disambig_match != 0u)
        return FL_NET_SERVER_MEMBER_ID_NONE;
    if (principal_count > 1u)
        return FL_NET_SERVER_MEMBER_ID_NONE; /* ambiguous */
    return hit;
}

size_t fl_net_client_member_count(const fl_net_client_t *client) {
    return client ? client->member_count : 0u;
}
const fl_net_client_member_t *
fl_net_client_member_at(const fl_net_client_t *client, size_t index) {
    if (!client || index >= client->member_count)
        return NULL;
    return &client->members[index];
}

fl_result_t fl_net_client_set_local_nick(fl_net_client_t *client,
                                         fl_net_server_member_id_t member_id,
                                         const char *local_nick) {
    if (!client)
        return FL_RESULT_INVAL;
    for (size_t i = 0; i < client->member_count; i++) {
        if (client->members[i].member_id == member_id) {
            memset(client->members[i].local_nick, 0,
                   sizeof(client->members[i].local_nick));
            if (local_nick && local_nick[0])
                strncpy(client->members[i].local_nick, local_nick,
                        sizeof(client->members[i].local_nick) - 1u);
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_NOENT;
}

fl_result_t fl_net_client_send_private(fl_net_client_t *client,
                                       fl_net_server_member_id_t recipient_id,
                                       const char *text) {
    uint8_t payload[FL_NET_SESSION_MAX_MSG];
    size_t tlen;
    if (!client || !text || recipient_id == FL_NET_SERVER_MEMBER_ID_NONE)
        return FL_RESULT_INVAL;
    if (client->state != FL_NET_CLIENT_STATE_CONNECTED ||
        client->peer_handle == FL_NET_SOCK_INVALID)
        return FL_RESULT_INVAL;
    /* Pre-check the cached roster so the caller gets FL_RESULT_NOENT
     * synchronously instead of waiting for an OP_ERR / silent drop from
     * the host. The roster is refreshed on every MEMBER_LIST_SNAPSHOT
     * (set_local_nick / dispatch_frame). The host self-id (1) is always
     * a valid recipient even when the snapshot hasn't arrived yet. */
    if (recipient_id != FL_NET_SERVER_MEMBER_ID_HOST) {
        int found = 0;
        for (size_t i = 0; i < client->member_count; i++) {
            if (client->members[i].member_id == recipient_id) {
                found = 1;
                break;
            }
        }
        if (!found)
            return FL_RESULT_NOENT;
    }
    /* 4-byte [sender_id][reserved] prefix sits in front of the text. */
    tlen = strnlen(text, (size_t)FL_NET_SESSION_MAX_MSG - 4u + 1u);
    if (tlen == 0u || tlen > (size_t)FL_NET_SESSION_MAX_MSG - 4u)
        return FL_RESULT_INVAL;
    payload[0] = (uint8_t)((recipient_id >> 8) & 0xFFu);
    payload[1] = (uint8_t)(recipient_id & 0xFFu);
    payload[2] = 0u;
    payload[3] = 0u;
    memcpy(payload + 4, text, tlen);
    s_client_msg_serial++;
    {
        char transfer_id[FL_SERVER_SHARE_ID_MAX];
        snprintf(transfer_id, sizeof(transfer_id), "msg-%u-%u",
                 (unsigned)client->assigned_member_id, s_client_msg_serial);
        (void)fl_net_msg_send_meta_direct(client, transfer_id, "direct",
                                          recipient_id, 0u, 0);
    }
    return fl_net_session_send_frame(client->peer_handle,
                                     (uint8_t)FL_NET_SESSION_OP_MSG_DIRECT,
                                     payload, (uint16_t)(4u + tlen));
}

fl_net_server_event_kind_t
fl_net_client_dispatch_frame(fl_net_client_t *client, uint8_t opcode,
                              const uint8_t *payload, uint16_t plen,
                              fl_net_client_event_cb cb, void *data) {
    fl_net_server_event_kind_t kind;
    fl_net_server_member_id_t mid = FL_NET_SERVER_MEMBER_ID_NONE;
    size_t text_off = 0;
    char text[FL_NET_SESSION_MAX_MSG + 1];
    size_t text_len;

    if (!client)
        return FL_NET_SERVER_EVENT_NONE;

    kind = opcode_to_event(opcode);
    if (opcode == (uint8_t)FL_NET_SESSION_OP_FILE_CHUNK) {
        (void)fl_net_file_store_chunk(payload, plen);
        return FL_NET_SERVER_EVENT_NONE;
    }
    if (opcode == (uint8_t)FL_NET_SESSION_OP_FILE_META) {
        (void)fl_net_file_store_meta(payload, plen);
        return FL_NET_SERVER_EVENT_NONE;
    }
    if (opcode == (uint8_t)FL_NET_SESSION_OP_MSG_META) {
        (void)fl_net_msg_store_meta(payload, plen);
        return FL_NET_SERVER_EVENT_NONE;
    }
    if (opcode == (uint8_t)FL_NET_SESSION_OP_FILE_DONE) {
        (void)fl_net_file_store_done(payload, plen);
        return FL_NET_SERVER_EVENT_NONE;
    }
    if (kind == FL_NET_SERVER_EVENT_NONE)
        return FL_NET_SERVER_EVENT_NONE;

    /* Refresh the cached roster before firing so a subsequent
     * `server connected` from the same shell sees the new state. */
    if (opcode == (uint8_t)FL_NET_SESSION_OP_MEMBER_LIST_SNAPSHOT) {
        client_decode_member_list(client, payload, plen);
        if (cb)
            cb(FL_NET_SERVER_EVENT_MEMBER_LIST, "",
               FL_NET_SERVER_MEMBER_ID_NONE, data);
        return FL_NET_SERVER_EVENT_MEMBER_LIST;
    }

    /* Structured prefixes. HOST_PROMOTE leaves full payload for the
     * shell to parse the trailing [ip_be][port] tuple. */
    if (opcode == (uint8_t)FL_NET_SESSION_OP_HELLO_ACK && plen >= 2u) {
        mid = (fl_net_server_member_id_t)
            (((uint16_t)payload[0] << 8) | payload[1]);
        text_off = 2u;
    }
    if (opcode == (uint8_t)FL_NET_SESSION_OP_MSG_DIRECT_DELIVER && plen >= 4u) {
        mid = (fl_net_server_member_id_t)
            (((uint16_t)payload[0] << 8) | payload[1]);
        text_off = 4u;
    }
    if (opcode == (uint8_t)FL_NET_SESSION_OP_CTRL_HOST_PROMOTE && plen >= 8u) {
        mid = (fl_net_server_member_id_t)
            (((uint16_t)payload[0] << 8) | payload[1]);
        text_off = 0u;
        client->last_host_promote_payload_len = plen;
        kind = (mid == client->assigned_member_id)
                   ? FL_NET_SERVER_EVENT_HOST_PROMOTE
                   : FL_NET_SERVER_EVENT_HOST_REDIRECT;
    }
    if (opcode == (uint8_t)FL_NET_SESSION_OP_CTRL_HOST_PROMOTE6 &&
        plen >= FL_NET_SESSION_CTRL_HOST_PROMOTE6_PAYLOAD_LEN) {
        mid = (fl_net_server_member_id_t)
            (((uint16_t)payload[0] << 8) | payload[1]);
        text_off = 0u;
        client->last_host_promote_payload_len = plen;
        kind = (mid == client->assigned_member_id)
                   ? FL_NET_SERVER_EVENT_HOST_PROMOTE
                   : FL_NET_SERVER_EVENT_HOST_REDIRECT;
    }
    if (opcode == (uint8_t)FL_NET_SESSION_OP_FILE_OFFER) {
        fl_server_file_offer_t offer;
        text[0] = '\0';
        text_len = 0u;
        if (fl_file_offer_wire_payload_revoked(payload, plen)) {
            kind = FL_NET_SERVER_EVENT_NONE;
        } else if (fl_file_packet_decode_offer(payload, plen, &offer) == FL_RESULT_OK) {
            if (offer.sender_member_id == client->assigned_member_id) {
                kind = FL_NET_SERVER_EVENT_NONE;
            } else {
                (void)fl_server_file_store_offer(&offer);
                if (fl_server_file_render_offer_line(&offer,
                                                     client->assigned_member_id,
                                                     text, sizeof(text)) == FL_RESULT_OK &&
                    text[0] != '\0') {
                    text_len = strlen(text);
                } else {
                    kind = FL_NET_SERVER_EVENT_NONE;
                }
            }
        } else {
            kind = FL_NET_SERVER_EVENT_NONE;
        }
    } else {
        text_len = plen > text_off ? (size_t)(plen - text_off) : 0u;
        if (text_len >= sizeof(text))
            text_len = sizeof(text) - 1u;
        if (text_len > 0u)
            memcpy(text, payload + text_off, text_len);
        text[text_len] = '\0';
    }

    if (cb && kind != FL_NET_SERVER_EVENT_NONE)
        cb(kind, text, mid, data);
    return kind;
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
        fl_net_server_event_kind_t kind;
        fl_result_t rc;

        /* Partial-frame-preserving recv: a TCP boundary that splits the
         * 6-byte header or a long body never looks like magic mismatch. */
        rc = fl_net_session_recv_frame_nb(client->peer_handle,
                                          &client->rx_state,
                                          &opcode, payload, sizeof(payload),
                                          &plen);
        if (rc == FL_RESULT_TIMEDOUT)
            break;
        if (rc == FL_RESULT_EOF || rc != FL_RESULT_OK) {
            client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
            if (client->peer_handle != FL_NET_SOCK_INVALID) {
                fl_net_sock_close(client->peer_handle);
                client->peer_handle = FL_NET_SOCK_INVALID;
            }
            fl_net_session_rx_reset(&client->rx_state);
            if (cb)
                cb(FL_NET_SERVER_EVENT_CLOSED, "",
                   FL_NET_SERVER_MEMBER_ID_NONE, data);
            dispatched++;
            break;
        }

        kind = fl_net_client_dispatch_frame(client, opcode, payload, plen,
                                            cb, data);
        if (kind != FL_NET_SERVER_EVENT_NONE)
            dispatched++;

        if (kind == FL_NET_SERVER_EVENT_CLOSED) {
            client->state = FL_NET_CLIENT_STATE_DISCONNECTED;
            if (client->peer_handle != FL_NET_SOCK_INVALID) {
                fl_net_sock_close(client->peer_handle);
                client->peer_handle = FL_NET_SOCK_INVALID;
            }
            fl_net_session_rx_reset(&client->rx_state);
            break;
        }
    }
    return dispatched;
}

fl_net_client_state_t fl_net_client_state(const fl_net_client_t *client) {
    return client ? client->state : FL_NET_CLIENT_STATE_DISCONNECTED;
}
