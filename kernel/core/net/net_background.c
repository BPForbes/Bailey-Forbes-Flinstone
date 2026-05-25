#include "net_background.h"

#include "workqueue.h"

static void net_bg_work(void *ctx) {
    (void)ctx;
    /* P3-14: RX dequeue, TCP timer wheel, delayed ACK — not implemented. */
}

static int s_net_bg_inited;

void fl_net_background_init(void) {
    s_net_bg_inited = 1;
}

void fl_net_background_shutdown(void) {
    s_net_bg_inited = 0;
}

fl_result_t fl_net_background_arp_tick_kick(void) {
    fl_wq_work_t w = {
        .fn = net_bg_work,
        .ctx = NULL,
        .tag = 0x0314u,
    };

    if (!s_net_bg_inited)
        fl_net_background_init();
    return fl_wq_enqueue(fl_wq_default(), &w);
}

void fl_net_background_tick(unsigned max_items) {
    (void)max_items;
    if (!s_net_bg_inited)
        return;
    /* Future: drain netdev RX rings, run TCP timers (**P3-7**). */
}
