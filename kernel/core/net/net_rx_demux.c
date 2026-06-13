#include "net_rx_demux.h"

#include "net_loopback.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_stack_sync.h"
#include "net_wire.h"

#include "contract_p3_ipv4.h"

static fl_result_t rx_demux_ipv4_frame(fl_net_driver_t *drv, const uint8_t *frame,
                                       size_t len) {
    size_t ip_off;
    size_t ip_len;
    uint32_t dst_be;
    uint8_t reply[FL_NET_WIRE_FRAME_BUF_MAX];
    size_t reply_len = 0;
    fl_net_route_entry_t route;
    fl_result_t rc;

    if (!drv || !frame)
        return FL_RESULT_INVAL;
    if (!fl_net_wire_parse_eth_ipv4(frame, len, &ip_off, &ip_len, &dst_be))
        return FL_RESULT_TIMEDOUT;
    if (fl_net_route_lookup(dst_be, &route) != FL_RESULT_OK || route.drv != drv)
        return FL_RESULT_TIMEDOUT;
    rc = fl_net_loopback_deliver_ipv4(frame + ip_off, ip_len, reply, sizeof(reply),
                                      &reply_len);
    if (rc != FL_RESULT_OK || reply_len == 0u)
        return rc;
    {
        fl_net_frame_view_t view;
        view.data = reply;
        view.len = reply_len;
        return fl_net_netdev_send(drv, &view);
    }
}

unsigned fl_net_rx_demux_poll(unsigned max_frames) {
    fl_net_driver_t *loopback;
    unsigned consumed = 0u;
    uint8_t buf[FL_NET_WIRE_FRAME_BUF_MAX];

    if (max_frames == 0u)
        max_frames = 8u;

    loopback = fl_net_netdev_loopback();
    if (!loopback || !loopback->recv)
        return 0u;

    fl_net_stack_lock();
    while (consumed < max_frames) {
        fl_net_frame_mut_t mut;
        fl_result_t rc;

        mut.data = buf;
        mut.cap = sizeof(buf);
        mut.len = 0u;
        rc = fl_net_netdev_recv(loopback, &mut, 0u);
        if (rc != FL_RESULT_OK)
            break;
        (void)rx_demux_ipv4_frame(loopback, buf, mut.len);
        consumed++;
    }
    fl_net_stack_unlock();
    return consumed;
}
