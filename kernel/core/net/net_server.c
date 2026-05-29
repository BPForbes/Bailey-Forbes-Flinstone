/*
 * net_server.c — host side of the P3-13 session protocol.
 *
 * Owns the TCP listener, the member registry (host inserted at
 * member_id 1 as a self-member with no socket), the wire codec used
 * by both server and client, and the announcement / chat / nick /
 * host-transfer dispatch. All writes go through one of:
 *
 *   broadcast_to_peers          — to every peer with a live socket
 *   broadcast_to_peers_except   — fan-out that skips the sender (chat)
 *   fl_net_session_send_frame   — point-to-point (private DM, errors)
 *
 * Reads happen one frame at a time per peer via the non-blocking
 * fl_net_session_recv_frame_nb helper, with per-slot rx state in
 * s_member_rx[] so a partial TCP segment never desyncs the parser.
 */
#include "net_server.h"

#include "fl_colors.h"
#include "net_socket.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#define FL_NET_SERVER_HOSTED 1
#endif

/* One partial-frame parser per member slot, parallel to srv->members[].
 * Kept out of the contract struct so fl_net_server_member_t stays stable. */
static fl_net_session_rx_t s_member_rx[FL_NET_SERVER_MAX_MEMBERS];

static void member_rx_reset_slot(size_t i) {
    if (i < FL_NET_SERVER_MAX_MEMBERS)
        fl_net_session_rx_reset(&s_member_rx[i]);
}

/* ------------------------------------------------------------------------- */
/* Wire codec                                                                */
/* ------------------------------------------------------------------------- */

/* Wire encoder: [magic(1)][version(1)][opcode(1)][flags(1)=0][len_be(2)]
 * followed by `payload_len` raw bytes. The reserved flags byte is always
 * emitted as 0; the matching recv path rejects non-zero on input. */
size_t fl_net_session_encode_frame(uint8_t *buf, size_t cap, uint8_t opcode,
                                   const uint8_t *payload, uint16_t payload_len) {
    size_t need;

    if (!buf)
        return 0;
    if (payload_len > FL_NET_SESSION_MAX_MSG)
        return 0;
    if (payload_len > 0u && !payload)
        return 0;

    need = (size_t)FL_NET_SESSION_HDR_LEN + (size_t)payload_len;
    if (cap < need)
        return 0;

    buf[0] = (uint8_t)FL_NET_SESSION_MAGIC;
    buf[1] = (uint8_t)FL_NET_SESSION_VERSION;
    buf[2] = opcode;
    buf[3] = 0u; /* flags reserved */
    buf[4] = (uint8_t)((payload_len >> 8) & 0xFFu);
    buf[5] = (uint8_t)(payload_len & 0xFFu);
    if (payload_len > 0u)
        memcpy(buf + FL_NET_SESSION_HDR_LEN, payload, payload_len);
    return need;
}

/* Loop fl_net_sock_recv until `n` bytes are in `out` or we hit an error.
 * The blocking variant of the recv path uses this directly; the
 * non-blocking poller does NOT (it would lose bytes on EAGAIN — see
 * fl_net_session_recv_frame_nb and its per-stream rx state). */
static fl_result_t recv_exact(fl_net_sock_handle_t handle, uint8_t *out, size_t n,
                              unsigned timeout_ms) {
    size_t total = 0;

    while (total < n) {
        size_t got = 0;
        fl_result_t rc = fl_net_sock_recv(handle, out + total, n - total, &got,
                                          timeout_ms);
        if (rc == FL_RESULT_EOF)
            return FL_RESULT_EOF;
        if (rc == FL_RESULT_TIMEDOUT)
            return FL_RESULT_TIMEDOUT;
        if (rc != FL_RESULT_OK)
            return rc;
        if (got == 0u)
            return FL_RESULT_EOF;
        total += got;
    }
    return FL_RESULT_OK;
}

fl_result_t fl_net_session_recv_frame(fl_net_sock_handle_t handle, uint8_t *opcode_out,
                                      uint8_t *payload_out, size_t payload_cap,
                                      uint16_t *payload_len_out,
                                      unsigned timeout_ms) {
    uint8_t hdr[FL_NET_SESSION_HDR_LEN];
    uint16_t plen;
    fl_result_t rc;

    if (!opcode_out || !payload_len_out)
        return FL_RESULT_INVAL;

    rc = recv_exact(handle, hdr, sizeof(hdr), timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    if (hdr[0] != (uint8_t)FL_NET_SESSION_MAGIC ||
        hdr[1] != (uint8_t)FL_NET_SESSION_VERSION ||
        hdr[3] != 0u) /* flags reserved; the encoder always emits 0 */
        return FL_RESULT_INVAL;
    *opcode_out = hdr[2];
    plen = (uint16_t)((uint16_t)hdr[4] << 8) | (uint16_t)hdr[5];
    if (plen > FL_NET_SESSION_MAX_MSG)
        return FL_RESULT_INVAL;
    *payload_len_out = plen;

    if (plen == 0u)
        return FL_RESULT_OK;

    if (payload_out && payload_cap >= plen) {
        rc = recv_exact(handle, payload_out, plen, timeout_ms);
        if (rc != FL_RESULT_OK)
            return rc;
    } else {
        /* Truncate into scratch; *payload_len_out keeps the true wire plen
         * so the caller can detect truncation via plen > payload_cap. */
        uint8_t scratch[64];
        size_t remaining = plen;

        if (payload_out && payload_cap > 0u) {
            rc = recv_exact(handle, payload_out, payload_cap, timeout_ms);
            if (rc != FL_RESULT_OK)
                return rc;
            remaining -= payload_cap;
        }
        while (remaining > 0u) {
            size_t chunk = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
            rc = recv_exact(handle, scratch, chunk, timeout_ms);
            if (rc != FL_RESULT_OK)
                return rc;
            remaining -= chunk;
        }
    }
    return FL_RESULT_OK;
}

void fl_net_session_rx_reset(fl_net_session_rx_t *st) {
    if (!st)
        return;
    st->hdr_filled = 0u;
    st->expected_plen = 0u;
    st->body_filled = 0u;
}

fl_result_t fl_net_session_recv_frame_nb(fl_net_sock_handle_t handle,
                                          fl_net_session_rx_t *st,
                                          uint8_t *opcode_out,
                                          uint8_t *payload_out, size_t payload_cap,
                                          uint16_t *payload_len_out) {
    if (!st || !opcode_out || !payload_len_out)
        return FL_RESULT_INVAL;

    /* Phase 1: header bytes — survive a mid-read FL_RESULT_TIMEDOUT via st. */
    while (st->hdr_filled < FL_NET_SESSION_HDR_LEN) {
        size_t want = (size_t)FL_NET_SESSION_HDR_LEN - st->hdr_filled;
        size_t got = 0;
        fl_result_t rc = fl_net_sock_recv(handle, st->hdr + st->hdr_filled,
                                          want, &got, 0u);
        if (rc == FL_RESULT_TIMEDOUT)
            return FL_RESULT_TIMEDOUT;
        if (rc == FL_RESULT_EOF)
            return FL_RESULT_EOF;
        if (rc != FL_RESULT_OK)
            return rc;
        if (got == 0u)
            return FL_RESULT_EOF;
        st->hdr_filled = (uint16_t)(st->hdr_filled + got);
    }

    /* Validate the completed header exactly once per frame. */
    if (st->body_filled == 0u && st->expected_plen == 0u) {
        if (st->hdr[0] != (uint8_t)FL_NET_SESSION_MAGIC ||
            st->hdr[1] != (uint8_t)FL_NET_SESSION_VERSION ||
            st->hdr[3] != 0u) {
            fl_net_session_rx_reset(st);
            return FL_RESULT_INVAL;
        }
        st->expected_plen = (uint16_t)(((uint16_t)st->hdr[4] << 8) | st->hdr[5]);
        if (st->expected_plen > FL_NET_SESSION_MAX_MSG) {
            fl_net_session_rx_reset(st);
            return FL_RESULT_INVAL;
        }
    }

    /* Phase 2: payload bytes (may be zero for a header-only frame). */
    while (st->body_filled < st->expected_plen) {
        size_t want = (size_t)st->expected_plen - st->body_filled;
        size_t got = 0;
        fl_result_t rc = fl_net_sock_recv(handle, st->body + st->body_filled,
                                          want, &got, 0u);
        if (rc == FL_RESULT_TIMEDOUT)
            return FL_RESULT_TIMEDOUT;
        if (rc == FL_RESULT_EOF)
            return FL_RESULT_EOF;
        if (rc != FL_RESULT_OK)
            return rc;
        if (got == 0u)
            return FL_RESULT_EOF;
        st->body_filled = (uint16_t)(st->body_filled + got);
    }

    /* Full frame ready — publish + reset for the next. */
    *opcode_out = st->hdr[2];
    *payload_len_out = st->expected_plen;
    if (payload_out && payload_cap > 0u && st->expected_plen > 0u) {
        size_t n = st->expected_plen < payload_cap ? st->expected_plen : payload_cap;
        memcpy(payload_out, st->body, n);
    }
    fl_net_session_rx_reset(st);
    return FL_RESULT_OK;
}

fl_result_t fl_net_session_send_frame(fl_net_sock_handle_t handle, uint8_t opcode,
                                      const uint8_t *payload, uint16_t payload_len) {
    uint8_t buf[FL_NET_SESSION_HDR_LEN + FL_NET_SESSION_MAX_MSG];
    size_t encoded;
    size_t total = 0;

    encoded = fl_net_session_encode_frame(buf, sizeof(buf), opcode, payload, payload_len);
    if (encoded == 0u)
        return FL_RESULT_INVAL;

    while (total < encoded) {
        size_t sent = 0;
        fl_result_t rc = fl_net_sock_send(handle, buf + total, encoded - total, &sent);
        if (rc != FL_RESULT_OK)
            return rc;
        if (sent == 0u)
            return FL_RESULT_ERR;
        total += sent;
    }
    return FL_RESULT_OK;
}

/* ------------------------------------------------------------------------- */
/* Member registry helpers                                                   */
/* ------------------------------------------------------------------------- */

/* Linear scan — bounded by FL_NET_SERVER_MAX_MEMBERS (16) so any
 * dictionary would be larger than the search it replaces. */
static fl_net_server_member_t *member_by_id(fl_net_server_t *srv,
                                            fl_net_server_member_id_t id) {
    if (!srv || id == FL_NET_SERVER_MEMBER_ID_NONE)
        return NULL;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        if (srv->members[i].in_use && srv->members[i].member_id == id)
            return &srv->members[i];
    }
    return NULL;
}

static const fl_net_server_member_t *
member_by_id_const(const fl_net_server_t *srv, fl_net_server_member_id_t id) {
    if (!srv || id == FL_NET_SERVER_MEMBER_ID_NONE)
        return NULL;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        if (srv->members[i].in_use && srv->members[i].member_id == id)
            return &srv->members[i];
    }
    return NULL;
}

static fl_net_server_member_t *member_alloc_slot(fl_net_server_t *srv) {
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        if (!srv->members[i].in_use)
            return &srv->members[i];
    }
    return NULL;
}

/* Count how many *other* members share principal `p`. */
static unsigned principal_count_other(const fl_net_server_t *srv,
                                      fl_net_server_member_id_t exclude_id,
                                      const char *p) {
    unsigned n = 0;
    if (!srv || !p)
        return 0;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        const fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (m->member_id == exclude_id)
            continue;
        if (strncmp(m->principal, p, FL_NET_SERVER_PRINCIPAL_MAX) == 0)
            n++;
    }
    return n;
}

/* Re-walk the registry to assign `{1}..{N}` to every member that shares
 * `principal`, or clear the marker when only one such member remains.
 * Called on join and drop so `flinstone {2}` leaving correctly reverts
 * the remaining `flinstone {1}` to plain `flinstone`. */
static void recompute_disambig(fl_net_server_t *srv, const char *principal) {
    unsigned same_count = 0;
    unsigned ordinal = 0;

    if (!srv || !principal || !principal[0])
        return;

    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        const fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (strncmp(m->principal, principal, FL_NET_SERVER_PRINCIPAL_MAX) == 0)
            same_count++;
    }

    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (strncmp(m->principal, principal, FL_NET_SERVER_PRINCIPAL_MAX) != 0)
            continue;
        if (same_count <= 1u) {
            m->disambig_index = 0u; /* singleton */
        } else {
            ordinal++;
            if (ordinal > 255u)
                ordinal = 255u;
            m->disambig_index = (uint8_t)ordinal;
        }
    }
}

/* Snapshot helper for member display. */
static void render_member_display(const fl_net_server_member_t *m, char *out, size_t cap) {
    if (!out || cap == 0u)
        return;
    out[0] = '\0';
    if (!m || !m->in_use)
        return;
    if (m->nick[0] != '\0') {
        snprintf(out, cap, "%s", m->nick);
        return;
    }
    if (m->disambig_index == 0u) {
        snprintf(out, cap, "%s", m->principal);
    } else {
        snprintf(out, cap, "%s {%u}", m->principal, (unsigned)m->disambig_index);
    }
}

fl_result_t fl_net_server_member_display(const fl_net_server_t *srv,
                                         fl_net_server_member_id_t member_id,
                                         char *out, size_t cap) {
    const fl_net_server_member_t *m = member_by_id_const(srv, member_id);
    if (!out || cap == 0u)
        return FL_RESULT_INVAL;
    if (!m) {
        out[0] = '\0';
        return FL_RESULT_NOENT;
    }
    render_member_display(m, out, cap);
    return FL_RESULT_OK;
}

size_t fl_net_server_member_count(const fl_net_server_t *srv) {
    size_t n = 0;
    if (!srv)
        return 0;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        if (srv->members[i].in_use)
            n++;
    }
    return n;
}

const fl_net_server_member_t *
fl_net_server_member_at(const fl_net_server_t *srv, size_t index) {
    size_t seen = 0;
    if (!srv)
        return NULL;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        if (!srv->members[i].in_use)
            continue;
        if (seen == index)
            return &srv->members[i];
        seen++;
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Broadcast / announcement helpers                                          */
/* ------------------------------------------------------------------------- */

/* Send one frame to every member whose peer_handle is valid (skips host's
 * own self-member which has peer_handle = FL_NET_SOCK_INVALID). Drops the
 * member on permanent send error and emits a follow-up leave announcement.
 */
static void broadcast_to_peers(fl_net_server_t *srv, uint8_t opcode,
                               const uint8_t *payload, uint16_t plen) {
    if (!srv)
        return;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (m->peer_handle == FL_NET_SOCK_INVALID)
            continue;
        (void)fl_net_session_send_frame(m->peer_handle, opcode, payload, plen);
    }
}

/* Broadcast variant that skips one member so the chat relay does not echo
 * a sender's own public message back to them. */
static void broadcast_to_peers_except(fl_net_server_t *srv,
                                      fl_net_server_member_id_t skip,
                                      uint8_t opcode,
                                      const uint8_t *payload, uint16_t plen) {
    if (!srv)
        return;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (m->peer_handle == FL_NET_SOCK_INVALID)
            continue;
        if (m->member_id == skip)
            continue;
        (void)fl_net_session_send_frame(m->peer_handle, opcode, payload, plen);
    }
}

/* Compose "[Server Announcement] <text>" locally on the host's terminal.
 * Peers render their own copy when they receive the announce opcode. */
static void emit_local_announce(const char *text) {
    if (!text || !text[0])
        return;
    fl_color_announce("%s", text);
}

fl_result_t fl_net_server_announce(fl_net_server_t *srv, const char *fmt, ...) {
    char line[FL_NET_SERVER_ANNOUNCEMENT_MAX];
    int n;
    va_list ap;

    if (!srv || !fmt)
        return FL_RESULT_INVAL;
    if (!srv->running)
        return FL_RESULT_INVAL;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return FL_RESULT_INVAL;
    if ((size_t)n >= sizeof(line))
        n = (int)sizeof(line) - 1;

    broadcast_to_peers(srv, (uint8_t)FL_NET_SESSION_OP_SERVER_ANNOUNCE,
                       (const uint8_t *)line, (uint16_t)n);
    emit_local_announce(line);
    return FL_RESULT_OK;
}

/* Emit a JOIN_ANNOUNCE wire frame + local print for the host. */
static void announce_join(fl_net_server_t *srv, fl_net_server_member_id_t id) {
    char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
    char line[FL_NET_SERVER_ANNOUNCEMENT_MAX];
    int n;

    if (fl_net_server_member_display(srv, id, disp, sizeof(disp)) != FL_RESULT_OK)
        return;
    n = snprintf(line, sizeof(line), "%s has joined.", disp);
    if (n <= 0)
        return;
    broadcast_to_peers(srv, (uint8_t)FL_NET_SESSION_OP_JOIN_ANNOUNCE,
                       (const uint8_t *)line, (uint16_t)(n >= (int)sizeof(line) ?
                                                          (int)sizeof(line) - 1 : n));
    emit_local_announce(line);
}

static void announce_leave(fl_net_server_t *srv, const char *display_name) {
    char line[FL_NET_SERVER_ANNOUNCEMENT_MAX];
    int n;
    if (!display_name || !display_name[0])
        return;
    n = snprintf(line, sizeof(line), "%s has left.", display_name);
    if (n <= 0)
        return;
    broadcast_to_peers(srv, (uint8_t)FL_NET_SESSION_OP_LEAVE_ANNOUNCE,
                       (const uint8_t *)line, (uint16_t)(n >= (int)sizeof(line) ?
                                                          (int)sizeof(line) - 1 : n));
    emit_local_announce(line);
}

static void announce_nick(fl_net_server_t *srv, const char *old_display,
                          const char *new_nick) {
    char line[FL_NET_SERVER_ANNOUNCEMENT_MAX];
    int n;
    if (!old_display || !new_nick)
        return;
    n = snprintf(line, sizeof(line),
                 "User %s has been nicked by host. Their nickname is \"%s\".",
                 old_display, new_nick);
    if (n <= 0)
        return;
    broadcast_to_peers(srv, (uint8_t)FL_NET_SESSION_OP_NICK_SET_ANNOUNCE,
                       (const uint8_t *)line, (uint16_t)(n >= (int)sizeof(line) ?
                                                          (int)sizeof(line) - 1 : n));
    emit_local_announce(line);
}

/* ------------------------------------------------------------------------- */
/* Public / private chat (server side)                                       */
/* ------------------------------------------------------------------------- */

fl_result_t fl_net_server_send_public(fl_net_server_t *srv, const char *text) {
    char line[FL_NET_SESSION_MAX_MSG];
    char host_disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
    int n;
    size_t tlen;

    if (!srv || !srv->running || !text)
        return FL_RESULT_INVAL;
    /* Peek one past so an exactly-MAX_MSG payload is accepted. */
    tlen = strnlen(text, (size_t)FL_NET_SESSION_MAX_MSG + 1u);
    if (tlen == 0u || tlen > (size_t)FL_NET_SESSION_MAX_MSG)
        return FL_RESULT_INVAL;
    render_member_display(member_by_id(srv, FL_NET_SERVER_MEMBER_ID_HOST),
                          host_disp, sizeof(host_disp));
    n = snprintf(line, sizeof(line), "%s: %.*s", host_disp,
                 (int)tlen, text);
    if (n <= 0)
        return FL_RESULT_INVAL;
    if ((size_t)n >= sizeof(line))
        n = (int)sizeof(line) - 1;
    /* Skip the host's own self-member (peer_handle = INVALID anyway, but
     * pass the id explicitly so the intent is clear: the host's local
     * terminal does not need its own message echoed back). */
    broadcast_to_peers_except(srv, FL_NET_SERVER_MEMBER_ID_HOST,
                              (uint8_t)FL_NET_SESSION_OP_MSG_BROADCAST,
                              (const uint8_t *)line, (uint16_t)n);
    return FL_RESULT_OK;
}

fl_result_t fl_net_server_send_private(fl_net_server_t *srv,
                                       fl_net_server_member_id_t recipient_id,
                                       const char *text) {
    fl_net_server_member_t *m;
    uint8_t payload[FL_NET_SESSION_MAX_MSG];
    size_t tlen;

    if (!srv || !srv->running || !text)
        return FL_RESULT_INVAL;
    m = member_by_id(srv, recipient_id);
    if (!m || m->peer_handle == FL_NET_SOCK_INVALID)
        return FL_RESULT_NOENT;
    /* 4-byte [sender_id][reserved] prefix sits in front of the text. */
    tlen = strnlen(text, (size_t)FL_NET_SESSION_MAX_MSG - 4u + 1u);
    if (tlen == 0u || tlen > (size_t)FL_NET_SESSION_MAX_MSG - 4u)
        return FL_RESULT_INVAL;
    /* Payload: [sender_member_id_be16][reserved_be16=0][text...] */
    payload[0] = 0u; /* host id 1: high byte */
    payload[1] = (uint8_t)FL_NET_SERVER_MEMBER_ID_HOST;
    payload[2] = 0u;
    payload[3] = 0u;
    memcpy(payload + 4, text, tlen);
    return fl_net_session_send_frame(m->peer_handle,
                                     (uint8_t)FL_NET_SESSION_OP_MSG_DIRECT_DELIVER,
                                     payload, (uint16_t)(4u + tlen));
}

/* ------------------------------------------------------------------------- */
/* Member list snapshot                                                      */
/* ------------------------------------------------------------------------- */

static size_t encode_member_list_snapshot(const fl_net_server_t *srv,
                                          uint8_t *buf, size_t cap) {
    size_t off = 0;
    if (!srv || !buf)
        return 0;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        const fl_net_server_member_t *m = &srv->members[i];
        size_t plen, nlen;
        if (!m->in_use)
            continue;
        plen = strnlen(m->principal, FL_NET_SERVER_PRINCIPAL_MAX);
        nlen = strnlen(m->nick, FL_NET_SERVER_NICK_MAX);
        if (plen > 255u) plen = 255u;
        if (nlen > 255u) nlen = 255u;
        if (off + 6u + plen + nlen > cap)
            break;
        buf[off++] = (uint8_t)((m->member_id >> 8) & 0xFFu);
        buf[off++] = (uint8_t)(m->member_id & 0xFFu);
        buf[off++] = m->is_host ? 1u : 0u;
        buf[off++] = m->disambig_index;
        buf[off++] = (uint8_t)plen;
        memcpy(buf + off, m->principal, plen);
        off += plen;
        buf[off++] = (uint8_t)nlen;
        memcpy(buf + off, m->nick, nlen);
        off += nlen;
    }
    return off;
}

fl_result_t fl_net_server_broadcast_member_list(fl_net_server_t *srv) {
    uint8_t buf[FL_NET_SESSION_MAX_MSG];
    size_t n;
    if (!srv || !srv->running)
        return FL_RESULT_INVAL;
    n = encode_member_list_snapshot(srv, buf, sizeof(buf));
    broadcast_to_peers(srv, (uint8_t)FL_NET_SESSION_OP_MEMBER_LIST_SNAPSHOT,
                       buf, (uint16_t)n);
    return FL_RESULT_OK;
}

/* ------------------------------------------------------------------------- */
/* Host transfer                                                             */
/* ------------------------------------------------------------------------- */

/* Successor selection rule: lowest non-host member_id with a live
 * socket. This is the "stack" promotion mode — the user who joined
 * earliest becomes the new host. A future map-style mode (explicit
 * host pick) is sketched in the contract but not in this train. */
fl_result_t fl_net_server_transfer_and_stop(fl_net_server_t *srv,
                                            fl_net_server_member_id_t *new_host_out) {
    fl_net_server_member_t *succ = NULL;
    fl_net_server_member_id_t lowest_id = FL_NET_SERVER_MEMBER_ID_NONE;

    if (!srv)
        return FL_RESULT_INVAL;
    if (new_host_out)
        *new_host_out = FL_NET_SERVER_MEMBER_ID_NONE;
    if (!srv->running) {
        /* Listener may still be open from a half-init; clean it up. */
        if (srv->listen_handle != FL_NET_SOCK_INVALID) {
            fl_net_sock_close(srv->listen_handle);
            srv->listen_handle = FL_NET_SOCK_INVALID;
        }
        return FL_RESULT_OK;
    }

    /* Pick the lowest non-host member id with a live socket. */
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use || m->is_host)
            continue;
        if (m->peer_handle == FL_NET_SOCK_INVALID)
            continue;
        if (lowest_id == FL_NET_SERVER_MEMBER_ID_NONE || m->member_id < lowest_id) {
            lowest_id = m->member_id;
            succ = m;
        }
    }

    if (succ) {
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
        char line[FL_NET_SERVER_ANNOUNCEMENT_MAX];
        int n;
        uint8_t payload[8];

        /* "[Server Announcement]: <display> is now the host" to all peers
         * (and locally to the host) before the listener is torn down. */
        render_member_display(succ, disp, sizeof(disp));
        n = snprintf(line, sizeof(line), "%s is now the host", disp);
        if (n > 0) {
            if ((size_t)n >= sizeof(line))
                n = (int)sizeof(line) - 1;
            broadcast_to_peers(srv,
                               (uint8_t)FL_NET_SESSION_OP_SERVER_ANNOUNCE,
                               (const uint8_t *)line, (uint16_t)n);
            emit_local_announce(line);
        }

        payload[0] = (uint8_t)((succ->member_id >> 8) & 0xFFu);
        payload[1] = (uint8_t)(succ->member_id & 0xFFu);
        /* peer_ip_be is network byte order; serialise raw bytes. */
        payload[2] = (uint8_t)((succ->peer_ip_be) & 0xFFu);
        payload[3] = (uint8_t)((succ->peer_ip_be >> 8) & 0xFFu);
        payload[4] = (uint8_t)((succ->peer_ip_be >> 16) & 0xFFu);
        payload[5] = (uint8_t)((succ->peer_ip_be >> 24) & 0xFFu);
        payload[6] = (uint8_t)((srv->bind_port_host >> 8) & 0xFFu);
        payload[7] = (uint8_t)(srv->bind_port_host & 0xFFu);
        broadcast_to_peers(srv, (uint8_t)FL_NET_SESSION_OP_CTRL_HOST_PROMOTE,
                           payload, (uint16_t)sizeof(payload));
        if (new_host_out)
            *new_host_out = succ->member_id;
    } else {
        /* No successor — just KILL everyone. */
        broadcast_to_peers(srv, (uint8_t)FL_NET_SESSION_OP_CTRL_KILL, NULL, 0u);
    }

    /* Close every member socket + listener. */
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (m->peer_handle != FL_NET_SOCK_INVALID) {
            fl_net_sock_close(m->peer_handle);
            m->peer_handle = FL_NET_SOCK_INVALID;
        }
        member_rx_reset_slot(i);
        m->in_use = 0u;
    }
    if (srv->listen_handle != FL_NET_SOCK_INVALID) {
        fl_net_sock_close(srv->listen_handle);
        srv->listen_handle = FL_NET_SOCK_INVALID;
    }
    srv->running = 0u;
    return succ ? FL_RESULT_OK : FL_RESULT_NOENT;
}

/* ------------------------------------------------------------------------- */
/* Nick validation                                                           */
/* ------------------------------------------------------------------------- */

static int nick_is_valid(const char *nick) {
    size_t len;
    if (!nick)
        return 0;
    len = strnlen(nick, FL_NET_SERVER_NICK_MAX);
    if (len == 0u || len >= FL_NET_SERVER_NICK_MAX)
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)nick[i];
        if (c < 0x20u || c == 0x7Fu)
            return 0; /* no controls */
    }
    return 1;
}

/* Reject when nick matches any *other* member's principal or current nick. */
static int nick_collides(const fl_net_server_t *srv,
                         fl_net_server_member_id_t self_id, const char *nick) {
    if (!srv || !nick)
        return 1;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        const fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (m->member_id == self_id)
            continue;
        if (strncmp(m->principal, nick, FL_NET_SERVER_PRINCIPAL_MAX) == 0)
            return 1;
        if (m->nick[0] != '\0' &&
            strncmp(m->nick, nick, FL_NET_SERVER_NICK_MAX) == 0)
            return 1;
    }
    return 0;
}

fl_result_t fl_net_server_set_host_nick(fl_net_server_t *srv,
                                        fl_net_server_member_id_t member_id,
                                        const char *nick) {
    fl_net_server_member_t *m;
    char old_display[FL_NET_SERVER_DISPLAY_NAME_MAX];

    if (!srv || !srv->running)
        return FL_RESULT_INVAL;
    if (!nick_is_valid(nick))
        return FL_RESULT_INVAL;
    m = member_by_id(srv, member_id);
    if (!m)
        return FL_RESULT_NOENT;
    if (nick_collides(srv, member_id, nick))
        return FL_RESULT_BUSY; /* shell renders as "[ERROR] nickname taken" */

    render_member_display(m, old_display, sizeof(old_display));
    memset(m->nick, 0, sizeof(m->nick));
    strncpy(m->nick, nick, sizeof(m->nick) - 1);

    announce_nick(srv, old_display, m->nick);
    (void)fl_net_server_broadcast_member_list(srv);
    return FL_RESULT_OK;
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

fl_result_t fl_net_server_init(fl_net_server_t *srv) {
    if (!srv)
        return FL_RESULT_INVAL;
    memset(srv, 0, sizeof(*srv));
    srv->listen_handle = FL_NET_SOCK_INVALID;
    srv->next_member_id = FL_NET_SERVER_MEMBER_ID_HOST;
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++)
        member_rx_reset_slot(i);
    return fl_net_sock_init();
}

fl_result_t fl_net_server_host_start(fl_net_server_t *srv,
                                     uint32_t bind_addr_be, uint16_t port_host,
                                     const char *host_principal) {
    fl_result_t rc;
    fl_net_sock_handle_t listen_h = FL_NET_SOCK_INVALID;
    fl_net_server_member_t *self;

    if (!srv || !host_principal || !host_principal[0])
        return FL_RESULT_INVAL;
    if (srv->running)
        return FL_RESULT_BUSY;

    rc = fl_net_server_init(srv);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = fl_net_sock_open(FL_NET_SOCK_TYPE_STREAM, &listen_h);
    if (rc == FL_RESULT_NOSYS)
        return FL_RESULT_NOSYS;
    if (rc != FL_RESULT_OK)
        return rc;

    rc = fl_net_sock_bind(listen_h, bind_addr_be, port_host);
    if (rc != FL_RESULT_OK) {
        fl_net_sock_close(listen_h);
        return rc;
    }
    rc = fl_net_sock_listen(listen_h, FL_NET_SOCK_DEFAULT_LISTEN_BACKLOG);
    if (rc != FL_RESULT_OK) {
        fl_net_sock_close(listen_h);
        return rc;
    }
    /* Non-blocking listener so accept_pending can poll without stalling. */
    (void)fl_net_sock_set_nonblock(listen_h, 1);

    /* Register host as member_id 1. */
    self = member_alloc_slot(srv);
    if (!self) {
        fl_net_sock_close(listen_h);
        return FL_RESULT_NOMEM;
    }
    memset(self, 0, sizeof(*self));
    self->in_use = 1u;
    self->is_host = 1u;
    self->member_id = FL_NET_SERVER_MEMBER_ID_HOST;
    self->peer_handle = FL_NET_SOCK_INVALID; /* host has no socket to itself */
    strncpy(self->principal, host_principal, sizeof(self->principal) - 1);

    recompute_disambig(srv, self->principal);

    srv->listen_handle = listen_h;
    srv->bind_addr_be = bind_addr_be;
    srv->bind_port_host = port_host;
    srv->next_member_id = (fl_net_server_member_id_t)(FL_NET_SERVER_MEMBER_ID_HOST + 1);
    srv->running = 1u;
    return FL_RESULT_OK;
}

fl_result_t fl_net_server_host_stop(fl_net_server_t *srv) {
    if (!srv)
        return FL_RESULT_INVAL;
    if (!srv->running) {
        if (srv->listen_handle != FL_NET_SOCK_INVALID) {
            fl_net_sock_close(srv->listen_handle);
            srv->listen_handle = FL_NET_SOCK_INVALID;
        }
        return FL_RESULT_OK;
    }

    /* Best-effort kill broadcast then close every peer + listener. */
    broadcast_to_peers(srv, (uint8_t)FL_NET_SESSION_OP_CTRL_KILL, NULL, 0u);
    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (m->peer_handle != FL_NET_SOCK_INVALID) {
            fl_net_sock_close(m->peer_handle);
            m->peer_handle = FL_NET_SOCK_INVALID;
        }
        member_rx_reset_slot(i);
        m->in_use = 0u;
    }
    if (srv->listen_handle != FL_NET_SOCK_INVALID) {
        fl_net_sock_close(srv->listen_handle);
        srv->listen_handle = FL_NET_SOCK_INVALID;
    }
    srv->running = 0u;
    return FL_RESULT_OK;
}

/* Try to send NICK_PROMPT to peer when its principal collides with someone
 * already connected. Best effort: a peer that ignores the prompt keeps its
 * disambiguated principal forever. */
static void maybe_send_nick_prompt(fl_net_server_t *srv,
                                   fl_net_server_member_t *m) {
    char text[FL_NET_SERVER_ANNOUNCEMENT_MAX];
    int n;
    if (!srv || !m)
        return;
    if (principal_count_other(srv, m->member_id, m->principal) == 0u)
        return;
    n = snprintf(text, sizeof(text),
                 "principal '%s' is in use; respond with HOST_NICK_SET to choose a nick",
                 m->principal);
    if (n <= 0)
        return;
    (void)fl_net_session_send_frame(m->peer_handle,
                                    (uint8_t)FL_NET_SESSION_OP_NICK_PROMPT,
                                    (const uint8_t *)text,
                                    (uint16_t)(n >= (int)sizeof(text) ?
                                                (int)sizeof(text) - 1 : n));
}

fl_result_t fl_net_server_accept_pending(fl_net_server_t *srv,
                                         char *display_out, size_t display_cap) {
    fl_result_t rc;
    fl_net_sock_handle_t client_h = FL_NET_SOCK_INVALID;
    fl_net_server_member_t *m;
    uint8_t opcode = 0;
    uint8_t payload[FL_NET_SESSION_MAX_MSG];
    uint16_t plen = 0;
    uint8_t ack_payload[2 + FL_NET_SERVER_DISPLAY_NAME_MAX];
    size_t ack_len;
    char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];

    if (!srv)
        return FL_RESULT_INVAL;
    if (!srv->running)
        return FL_RESULT_INVAL;

    rc = fl_net_sock_accept(srv->listen_handle, &client_h);
    if (rc == FL_RESULT_NOSYS)
        return FL_RESULT_NOSYS;
    if (rc != FL_RESULT_OK)
        return FL_RESULT_TIMEDOUT; /* nothing pending */

    /* Read HELLO from new peer. */
    rc = fl_net_session_recv_frame(client_h, &opcode, payload, sizeof(payload),
                                   &plen, 2000u);
    if (rc != FL_RESULT_OK || opcode != (uint8_t)FL_NET_SESSION_OP_HELLO ||
        plen == 0u || plen >= FL_NET_SERVER_PRINCIPAL_MAX) {
        fl_net_sock_close(client_h);
        return FL_RESULT_INVAL;
    }

    m = member_alloc_slot(srv);
    if (!m) {
        /* Capacity exhausted — let the peer know politely. */
        const char err[] = "session full";
        (void)fl_net_session_send_frame(client_h, (uint8_t)FL_NET_SESSION_OP_ERR,
                                        (const uint8_t *)err, (uint16_t)(sizeof(err) - 1u));
        fl_net_sock_close(client_h);
        return FL_RESULT_NOMEM;
    }
    member_rx_reset_slot((size_t)(m - srv->members));
    memset(m, 0, sizeof(*m));
    m->in_use = 1u;
    m->is_host = 0u;
    m->member_id = srv->next_member_id++;
    if (srv->next_member_id == FL_NET_SERVER_MEMBER_ID_NONE)
        srv->next_member_id = (fl_net_server_member_id_t)(FL_NET_SERVER_MEMBER_ID_HOST + 1);
    m->peer_handle = client_h;
    (void)fl_net_sock_peer_ipv4(client_h, &m->peer_ip_be); /* best-effort */
    /* payload may not be NUL-terminated on the wire. */
    {
        size_t n = plen < (sizeof(m->principal) - 1u) ? plen : sizeof(m->principal) - 1u;
        memcpy(m->principal, payload, n);
        m->principal[n] = '\0';
    }

    recompute_disambig(srv, m->principal);

    /* HELLO_ACK: 2-byte member_id BE + UTF-8 display name. */
    render_member_display(m, disp, sizeof(disp));
    ack_payload[0] = (uint8_t)((m->member_id >> 8) & 0xFFu);
    ack_payload[1] = (uint8_t)(m->member_id & 0xFFu);
    {
        size_t dn = strnlen(disp, sizeof(disp));
        if (dn > sizeof(ack_payload) - 2u)
            dn = sizeof(ack_payload) - 2u;
        memcpy(ack_payload + 2, disp, dn);
        ack_len = 2u + dn;
    }
    (void)fl_net_session_send_frame(client_h, (uint8_t)FL_NET_SESSION_OP_HELLO_ACK,
                                    ack_payload, (uint16_t)ack_len);

    /* Optional NICK_PROMPT before join announcement when there's a collision.
     * Per the contract sequencing rule the JOIN_ANNOUNCE fires first. */
    announce_join(srv, m->member_id);
    maybe_send_nick_prompt(srv, m);
    /* Snapshot the roster so the new peer (and existing peers) can resolve
     * private-message senders + render `server connected`. */
    (void)fl_net_server_broadcast_member_list(srv);

    if (display_out && display_cap > 0u) {
        size_t dn = strnlen(disp, sizeof(disp));
        if (dn >= display_cap)
            dn = display_cap - 1u;
        memcpy(display_out, disp, dn);
        display_out[dn] = '\0';
    }
    return FL_RESULT_OK;
}

/* ------------------------------------------------------------------------- */
/* Inbound frame dispatch                                                    */
/* ------------------------------------------------------------------------- */

/* Drop a member after EOF or fatal error; emit a leave announcement using
 * the member's current display name (computed before the slot is cleared). */
static void drop_member(fl_net_server_t *srv, fl_net_server_member_t *m) {
    char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
    char principal_copy[FL_NET_SERVER_PRINCIPAL_MAX];

    if (!srv || !m || !m->in_use)
        return;
    render_member_display(m, disp, sizeof(disp));
    strncpy(principal_copy, m->principal, sizeof(principal_copy) - 1);
    principal_copy[sizeof(principal_copy) - 1] = '\0';

    if (m->peer_handle != FL_NET_SOCK_INVALID) {
        fl_net_sock_close(m->peer_handle);
        m->peer_handle = FL_NET_SOCK_INVALID;
    }
    member_rx_reset_slot((size_t)(m - srv->members));
    m->in_use = 0u;
    m->member_id = FL_NET_SERVER_MEMBER_ID_NONE;

    recompute_disambig(srv, principal_copy);
    announce_leave(srv, disp);
    (void)fl_net_server_broadcast_member_list(srv);
}

/* One incoming frame from member `m`. Returns 1 when the member should be
 * dropped, 0 otherwise. */
static int dispatch_member_frame(fl_net_server_t *srv, fl_net_server_member_t *m,
                                 uint8_t opcode, const uint8_t *payload,
                                 uint16_t plen) {
    char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];

    switch (opcode) {
    case FL_NET_SESSION_OP_MSG: {
        /* "Sender: text" relayed to everyone except the sender. */
        char line[FL_NET_SESSION_MAX_MSG];
        int n;
        render_member_display(m, disp, sizeof(disp));
        n = snprintf(line, sizeof(line), "%s: %.*s", disp, (int)plen,
                     plen > 0u ? (const char *)payload : "");
        if (n <= 0)
            return 0;
        if ((size_t)n >= sizeof(line))
            n = (int)sizeof(line) - 1;
        broadcast_to_peers_except(srv, m->member_id,
                                  (uint8_t)FL_NET_SESSION_OP_MSG_BROADCAST,
                                  (const uint8_t *)line, (uint16_t)n);
        return 0;
    }
    case FL_NET_SESSION_OP_HOST_NICK_SET: {
        /* Member-driven nick request (e.g. response to NICK_PROMPT). */
        char nick[FL_NET_SERVER_NICK_MAX];
        size_t n;
        if (plen == 0u || plen >= FL_NET_SERVER_NICK_MAX) {
            const char err[] = "nick rejected: invalid length";
            (void)fl_net_session_send_frame(m->peer_handle,
                                            (uint8_t)FL_NET_SESSION_OP_ERR,
                                            (const uint8_t *)err,
                                            (uint16_t)(sizeof(err) - 1u));
            return 0;
        }
        n = plen < sizeof(nick) - 1u ? plen : sizeof(nick) - 1u;
        memcpy(nick, payload, n);
        nick[n] = '\0';
        if (fl_net_server_set_host_nick(srv, m->member_id, nick) != FL_RESULT_OK) {
            const char err[] = "nick rejected: collision or invalid";
            (void)fl_net_session_send_frame(m->peer_handle,
                                            (uint8_t)FL_NET_SESSION_OP_ERR,
                                            (const uint8_t *)err,
                                            (uint16_t)(sizeof(err) - 1u));
        }
        return 0;
    }
    case FL_NET_SESSION_OP_MSG_DIRECT: {
        /* Private chat: payload = [recipient_id_be16][reserved_be16][text]. */
        fl_net_server_member_id_t recipient_id;
        fl_net_server_member_t *to;
        const char *text;
        uint16_t text_len;
        uint8_t deliver[FL_NET_SESSION_MAX_MSG];

        if (plen < 4u)
            return 0;
        recipient_id = (fl_net_server_member_id_t)
            (((uint16_t)payload[0] << 8) | payload[1]);
        text = (const char *)(payload + 4);
        text_len = (uint16_t)(plen - 4u);
        to = member_by_id(srv, recipient_id);
        if (!to || to->member_id == m->member_id ||
            to->peer_handle == FL_NET_SOCK_INVALID) {
            char err[64];
            int en = snprintf(err, sizeof(err),
                              "no such recipient member_id %u",
                              (unsigned)recipient_id);
            if (en > 0)
                (void)fl_net_session_send_frame(m->peer_handle,
                                                (uint8_t)FL_NET_SESSION_OP_ERR,
                                                (const uint8_t *)err,
                                                (uint16_t)en);
            return 0;
        }
        if (text_len > sizeof(deliver) - 4u)
            text_len = (uint16_t)(sizeof(deliver) - 4u);
        deliver[0] = (uint8_t)((m->member_id >> 8) & 0xFFu);
        deliver[1] = (uint8_t)(m->member_id & 0xFFu);
        deliver[2] = 0u;
        deliver[3] = 0u;
        memcpy(deliver + 4, text, text_len);
        (void)fl_net_session_send_frame(to->peer_handle,
                                        (uint8_t)FL_NET_SESSION_OP_MSG_DIRECT_DELIVER,
                                        deliver, (uint16_t)(4u + text_len));
        return 0;
    }
    case FL_NET_SESSION_OP_CTRL_LEAVE:
        return 1;
    case FL_NET_SESSION_OP_CTRL_KILL:
        /* Only the host may issue KILL; ignore here. Members get KILL pushed
         * to them via fl_net_server_host_stop. */
        return 0;
    default:
        /* Foundation build does not yet handle file opcodes or unknown ops. */
        return 0;
    }
}

fl_result_t fl_net_server_poll_members(fl_net_server_t *srv) {
    if (!srv)
        return FL_RESULT_INVAL;
    if (!srv->running)
        return FL_RESULT_INVAL;

    for (size_t i = 0; i < FL_NET_SERVER_MAX_MEMBERS; i++) {
        fl_net_server_member_t *m = &srv->members[i];
        if (!m->in_use)
            continue;
        if (m->peer_handle == FL_NET_SOCK_INVALID)
            continue; /* host self-member */

        /* Drain up to a small batch per poll; per-slot s_member_rx[i]
         * preserves partial frames across calls so TCP segmentation never
         * looks like a magic-byte mismatch. */
        (void)fl_net_sock_set_nonblock(m->peer_handle, 1);
        for (unsigned drained = 0; drained < 8u; drained++) {
            uint8_t opcode = 0;
            uint8_t payload[FL_NET_SESSION_MAX_MSG];
            uint16_t plen = 0;
            fl_result_t rc = fl_net_session_recv_frame_nb(
                m->peer_handle, &s_member_rx[i],
                &opcode, payload, sizeof(payload), &plen);
            if (rc == FL_RESULT_TIMEDOUT)
                break;
            if (rc == FL_RESULT_EOF || rc != FL_RESULT_OK) {
                drop_member(srv, m);
                break;
            }
            if (dispatch_member_frame(srv, m, opcode, payload, plen)) {
                drop_member(srv, m);
                break;
            }
        }
    }
    return FL_RESULT_OK;
}

#if !defined(FL_NET_SERVER_HOSTED)
/* Empty translation unit hint to avoid unused-static warnings on bare-metal:
 * the implementations above already gate through fl_net_sock_* which returns
 * FL_RESULT_NOSYS for us; nothing extra to do here. */
#endif
