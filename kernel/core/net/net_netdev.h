#ifndef NET_NETDEV_H
#define NET_NETDEV_H

#include "contract_p2_authz.h"
#include "fl/driver/net.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Optional **P2-3** gate for **P3-1** / **P3-3** (NULL allows in unit tests). */
typedef fl_authz_decision_t (*fl_net_netdev_authz_fn)(unsigned op, void *ctx);

void fl_net_netdev_init(void);
void fl_net_netdev_set_authz_hook(fl_net_netdev_authz_fn fn, void *ctx);

fl_result_t fl_net_netdev_authz_check(unsigned op);

/** **P3-2** software loopback **fl_net_driver_t** (always available). */
fl_net_driver_t *fl_net_netdev_loopback(void);

/** **P3-3** TAP driver; **NULL** until **fl_net_netdev_tap_open** succeeds. */
fl_net_driver_t *fl_net_netdev_tap(void);

fl_result_t fl_net_netdev_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame);
fl_result_t fl_net_netdev_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out,
                               unsigned timeout_ms);

/** Open hosted TAP (**P3-3**). Requires **FL_AUTHZ_OP_NETDEV_REGISTER**. */
fl_result_t fl_net_netdev_tap_open(const char *ifname_hint);
void fl_net_netdev_tap_close(void);
int fl_net_netdev_tap_is_open(void);

/** When TAP open failed, human-readable reason for CI logs (may be empty). */
const char *fl_net_netdev_tap_last_error(void);

typedef struct {
    uint64_t tx_frames;
    uint64_t rx_frames;
} fl_net_netdev_stats_t;

void fl_net_netdev_stats(fl_net_driver_t *drv, fl_net_netdev_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NET_NETDEV_H */
