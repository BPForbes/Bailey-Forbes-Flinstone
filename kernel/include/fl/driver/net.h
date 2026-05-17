/**
 * Network driver — **P3-1** data path (see **contracts/networking/contract_p3_netdev.h**).
 *
 * Send and recv use **fl_net_frame_view_t** and **fl_net_frame_mut_t** from
 * **contract_p3_wire.h**. Privileged open and raw I/O require **P2-3** op codes
 * **FL_AUTHZ_OP_NETDEV_REGISTER** and **FL_AUTHZ_OP_NETDEV_IO** via **contract_p3_trust.h**;
 * this header does not pull **contract_identity.h**.
 */
#ifndef FL_DRIVER_NET_H
#define FL_DRIVER_NET_H

#include "contract_p3_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fl_net_driver fl_net_driver_t;
struct fl_net_driver {
    fl_result_t (*send)(fl_net_driver_t *drv, const fl_net_frame_view_t *frame);
    /**
     * On **FL_RESULT_OK**, sets **out->len** received bytes (<= **out->cap**).
     * **out->data** and **out->cap** are inputs; **out->len** is output.
     */
    fl_result_t (*recv)(fl_net_driver_t *drv, fl_net_frame_mut_t *out);
    /** L2 payload MTU; default **FL_NET_ETH_MTU_DEFAULT** when unset by attach. */
    unsigned mtu;
    fl_net_drv_caps_t advertised_caps;
    void *impl;
};

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_NET_H */
