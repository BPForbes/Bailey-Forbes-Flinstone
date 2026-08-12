#ifndef NET_TAP_H
#define NET_TAP_H

#include "contract_result.h"
#include "fl/driver/net.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Default TAP name pattern for **TUNSETIFF** when **ifname_hint** is empty.
 * Kernel expands **%d**. Do not use **fl0** — that name is the macvlan LAN iface.
 */
#define FL_NET_TAP_IFNAME_PATTERN "fltap%d"

_Static_assert(sizeof(FL_NET_TAP_IFNAME_PATTERN) <= 16,
               "TAP name pattern must fit IFNAMSIZ");

/** **P3-3** TAP **send** / **recv** driver ops (requires open TAP fd in **impl**). */
fl_result_t fl_net_tap_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame);
fl_result_t fl_net_tap_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out);

fl_result_t fl_net_tap_open(const char *ifname_hint, int *out_fd, char ifname_out[16]);
void fl_net_tap_close(int fd);

#if defined(__linux__)
fl_result_t fl_net_tap_hwaddr(int fd, const char *ifname, uint8_t mac_out[6]);
#endif

uint64_t fl_net_tap_stat_tx(void);
uint64_t fl_net_tap_stat_rx(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_TAP_H */
