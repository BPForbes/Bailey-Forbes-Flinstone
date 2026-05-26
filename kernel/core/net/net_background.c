#include "net_background.h"

#include "fl/ipc.h"
#include "fl/mem_asm.h"

#include "workqueue.h"

#include "net_packet.h"

#include <pthread.h>

typedef struct {
    uint16_t payload_len;
    uint8_t payload[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX];
} fl_net_task_backend_inbox_msg_t;

_Static_assert(sizeof(fl_net_task_backend_inbox_msg_t) > 2u,
               "inbox msg must include at least len field");

static msgq_t *s_user_inboxes[FL_NET_TASK_BACKEND_MAX_USERS];
static unsigned s_user_inbox_msg_size;
static pthread_mutex_t s_user_inboxes_mu = PTHREAD_MUTEX_INITIALIZER;

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

    pthread_mutex_lock(&s_user_inboxes_mu);
    for (unsigned i = 0; i < FL_NET_TASK_BACKEND_MAX_USERS; i++) {
        if (s_user_inboxes[i]) {
            msgq_destroy(s_user_inboxes[i]);
            s_user_inboxes[i] = NULL;
        }
    }
    pthread_mutex_unlock(&s_user_inboxes_mu);
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
    (void)max_items;
    if (!s_net_bg_inited)
        fl_net_background_init();

    /* P3-14: do not recv from fl_net_netdev_loopback() here — that queue is shared
     * with request/response paths (ICMP/TCP probes in net_wire_egress). Draining it
     * in the background tick steals replies and causes intermittent timeouts.
     *
     * Connected-user delivery uses fl_net_task_backend_send_packet() directly until
     * P3-13 server/socket demux owns RX (see docs/P3_13_CHAT_SERVER.md).
     */
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

    pthread_mutex_lock(&s_user_inboxes_mu);
    if (s_user_inboxes[slot]) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_OK;
    }

    /* msgq_create requires fixed message_size. */
    s_user_inbox_msg_size = (unsigned)sizeof(msg_tmp);
    msgq_t *q = msgq_create(max_inbox_messages, (size_t)s_user_inbox_msg_size);
    if (!q) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_NOMEM;
    }

    s_user_inboxes[slot] = q;
    pthread_mutex_unlock(&s_user_inboxes_mu);
    return FL_RESULT_OK;
}

void fl_net_task_backend_user_close(unsigned slot) {
    slot = user_slot_sanity(slot);
    if (slot == (unsigned)-1)
        return;
    pthread_mutex_lock(&s_user_inboxes_mu);
    if (s_user_inboxes[slot]) {
        msgq_destroy(s_user_inboxes[slot]);
        s_user_inboxes[slot] = NULL;
    }
    pthread_mutex_unlock(&s_user_inboxes_mu);
}

fl_result_t fl_net_task_backend_send_packet(unsigned dst_slot, const fl_net_packet_t *pkt) {
    fl_net_task_backend_inbox_msg_t msg;

    if (!s_net_bg_inited)
        fl_net_background_init();

    dst_slot = user_slot_sanity(dst_slot);
    if (dst_slot == (unsigned)-1 || !pkt)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_user_inboxes_mu);
    msgq_t *q = s_user_inboxes[dst_slot];
    if (q == NULL) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_NOENT;
    }

    if ((pkt->valid & FL_NET_PKT_VALID_L4) == 0u) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_INVAL;
    }
    if (pkt->l4.len > FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_INVAL;
    }

    asm_mem_zero(&msg, sizeof(msg));
    msg.payload_len = (uint16_t)pkt->l4.len;

    {
        size_t out_len = 0;
        fl_result_t rc = fl_net_packet_copy_l4(pkt, msg.payload, sizeof(msg.payload), &out_len);
        if (rc != FL_RESULT_OK) {
            pthread_mutex_unlock(&s_user_inboxes_mu);
            return rc;
        }
        if (out_len != pkt->l4.len) {
            pthread_mutex_unlock(&s_user_inboxes_mu);
            return FL_RESULT_ERR;
        }
    }

    if (msgq_send(q, &msg, (size_t)sizeof(msg)) != 0) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_BUSY;
    }
    pthread_mutex_unlock(&s_user_inboxes_mu);
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
    pthread_mutex_lock(&s_user_inboxes_mu);
    msgq_t *q = s_user_inboxes[src_slot];
    if (q == NULL) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_NOENT;
    }

    if (cap == 0u) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_INVAL;
    }

    if (msgq_receive(q, &msg, (size_t)sizeof(msg), 0u) != 0) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_TIMEDOUT;
    }
    pthread_mutex_unlock(&s_user_inboxes_mu);

    if (msg.payload_len > cap)
        return FL_RESULT_INVAL;

    asm_mem_copy(out, msg.payload, (size_t)msg.payload_len);
    *out_len = (size_t)msg.payload_len;
    return FL_RESULT_OK;
}
