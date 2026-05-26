#include "net_background.h"

#include "fl/ipc.h"
#include "fl/mem_asm.h"

#include "workqueue.h"

#include "net_netdev.h"
#include "net_packet.h"
#include "net_wire.h"

typedef struct {
    uint16_t payload_len;
    uint8_t payload[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX];
} fl_net_task_backend_inbox_msg_t;

_Static_assert(sizeof(fl_net_task_backend_inbox_msg_t) > 2u,
               "inbox msg must include at least len field");

static msgq_t *s_user_inboxes[FL_NET_TASK_BACKEND_MAX_USERS];
static unsigned s_user_inbox_msg_size;

static void net_bg_work(void *ctx) {
    (void)ctx;
    /* P3-14: RX dequeue, TCP timer wheel, delayed ACK — not implemented. */
}

static int s_net_bg_inited;

static unsigned user_slot_sanity(unsigned slot) {
    return slot < FL_NET_TASK_BACKEND_MAX_USERS ? slot : (unsigned)-1;
}

void fl_net_background_init(void) {
    s_net_bg_inited = 1;

    /* msgq backend state lives outside inbox registration; initialize globals. */
    for (unsigned i = 0; i < FL_NET_TASK_BACKEND_MAX_USERS; i++)
        s_user_inboxes[i] = NULL;
    s_user_inbox_msg_size = (unsigned)sizeof(fl_net_task_backend_inbox_msg_t);
}

void fl_net_background_shutdown(void) {
    s_net_bg_inited = 0;

    for (unsigned i = 0; i < FL_NET_TASK_BACKEND_MAX_USERS; i++) {
        if (s_user_inboxes[i]) {
            msgq_destroy(s_user_inboxes[i]);
            s_user_inboxes[i] = NULL;
        }
    }
}

fl_result_t fl_net_background_arp_tick_kick(void) {
    fl_wq_work_t w = {
        .fn = net_bg_work,
        .ctx = NULL,
        .tag = FL_NET_BG_TAG_ARP_TICK,
        .pq_layer = FL_WQ_LAYER_BACKGROUND,
    };

    if (!s_net_bg_inited)
        fl_net_background_init();
    return fl_wq_enqueue(fl_wq_default(), &w);
}

void fl_net_background_tick(unsigned max_items) {
    if (!s_net_bg_inited)
        fl_net_background_init();

    /* P3-14 groundwork: packet RX -> connected-user inbox delivery.
     *
     * Note: server/message component is not implemented yet; inbox routing
     * uses a simple heuristic so the packet payload is already flowing
     * through the intended backend shape.
     */
    for (unsigned i = 0; i < max_items; i++) {
        uint8_t frame[FL_NET_WIRE_FRAME_BUF_MAX];
        fl_net_frame_mut_t mut;
        fl_result_t rc;

        mut.data = frame;
        mut.cap = sizeof(frame);
        mut.len = 0;

        rc = fl_net_netdev_recv(fl_net_netdev_loopback(), &mut, 0u);
        if (rc == FL_RESULT_TIMEDOUT)
            break;
        if (rc != FL_RESULT_OK)
            continue;

        fl_net_pipeline_rx_t pipe;
        fl_net_pipeline_rx_reset(&pipe);
        rc = fl_net_pipeline_rx_feed(&pipe, FL_NET_PIPE_STAGE_PARSE_L4, frame, mut.len);
        if (rc != FL_RESULT_OK)
            continue;
        if ((pipe.pkt.valid & FL_NET_PKT_VALID_L4) == 0u)
            continue;

        /* Simple routing heuristic (v0): map UDP/TCP destination port -> slot.
         * Fallback: map dst IPv4 last octet -> slot.
         */
        unsigned dst_slot = 0u;
        if (pipe.pkt.ip_proto == FL_NET_IP_PROTO_UDP || pipe.pkt.ip_proto == FL_NET_IP_PROTO_TCP) {
            if (pipe.pkt.l4.len >= 4u) {
                const uint8_t *l4 = pipe.pkt.frame.data + pipe.pkt.l4.off;
                uint16_t dst_port = (uint16_t)((l4[2] << 8) | l4[3]);
                dst_slot = (unsigned)dst_port % FL_NET_TASK_BACKEND_MAX_USERS;
            }
        } else {
            dst_slot = (unsigned)(pipe.pkt.dst_be & 0xffu) % FL_NET_TASK_BACKEND_MAX_USERS;
        }

        (void)fl_net_task_backend_send_packet(dst_slot, &pipe.pkt);
    }
}

fl_result_t fl_net_task_backend_user_open(unsigned slot, unsigned max_inbox_messages) {
    fl_net_task_backend_inbox_msg_t msg_tmp;

    if (!s_net_bg_inited)
        fl_net_background_init();

    slot = user_slot_sanity(slot);
    if (slot == (unsigned)-1)
        return FL_RESULT_INVAL;
    if (max_inbox_messages == 0u)
        max_inbox_messages = 4u;

    if (s_user_inboxes[slot])
        return FL_RESULT_OK;

    /* msgq_create requires fixed message_size. */
    s_user_inbox_msg_size = (unsigned)sizeof(msg_tmp);
    msgq_t *q = msgq_create(max_inbox_messages, (size_t)s_user_inbox_msg_size);
    if (!q)
        return FL_RESULT_NOMEM;

    s_user_inboxes[slot] = q;
    return FL_RESULT_OK;
}

void fl_net_task_backend_user_close(unsigned slot) {
    slot = user_slot_sanity(slot);
    if (slot == (unsigned)-1)
        return;
    if (s_user_inboxes[slot]) {
        msgq_destroy(s_user_inboxes[slot]);
        s_user_inboxes[slot] = NULL;
    }
}

fl_result_t fl_net_task_backend_send_packet(unsigned dst_slot, const fl_net_packet_t *pkt) {
    fl_net_task_backend_inbox_msg_t msg;

    if (!s_net_bg_inited)
        fl_net_background_init();

    dst_slot = user_slot_sanity(dst_slot);
    if (dst_slot == (unsigned)-1 || !pkt)
        return FL_RESULT_INVAL;
    if (s_user_inboxes[dst_slot] == NULL)
        return FL_RESULT_NOENT;

    if ((pkt->valid & FL_NET_PKT_VALID_L4) == 0u)
        return FL_RESULT_INVAL;
    if (pkt->l4.len > FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX)
        return FL_RESULT_INVAL;

    asm_mem_zero(&msg, sizeof(msg));
    msg.payload_len = (uint16_t)pkt->l4.len;

    {
        size_t out_len = 0;
        fl_result_t rc = fl_net_packet_copy_l4(pkt, msg.payload, sizeof(msg.payload), &out_len);
        if (rc != FL_RESULT_OK)
            return rc;
        if (out_len != pkt->l4.len)
            return FL_RESULT_ERR;
    }

    if (msgq_send(s_user_inboxes[dst_slot], &msg, (size_t)sizeof(msg)) != 0)
        return FL_RESULT_BUSY;
    return FL_RESULT_OK;
}

fl_result_t fl_net_task_backend_recv_packet(unsigned src_slot, uint8_t *out, size_t cap,
                                              size_t *out_len) {
    fl_net_task_backend_inbox_msg_t msg;

    if (!s_net_bg_inited)
        fl_net_background_init();

    src_slot = user_slot_sanity(src_slot);
    if (src_slot == (unsigned)-1 || !out || !out_len)
        return FL_RESULT_INVAL;
    if (s_user_inboxes[src_slot] == NULL)
        return FL_RESULT_NOENT;

    if (cap == 0u)
        return FL_RESULT_INVAL;

    if (msgq_receive(s_user_inboxes[src_slot], &msg, (size_t)sizeof(msg), 0u) != 0)
        return FL_RESULT_TIMEDOUT;

    if (msg.payload_len > cap)
        return FL_RESULT_INVAL;

    asm_mem_copy(out, msg.payload, (size_t)msg.payload_len);
    *out_len = (size_t)msg.payload_len;
    return FL_RESULT_OK;
}
