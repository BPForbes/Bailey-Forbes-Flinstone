/**
 * Network driver interface - placeholder for MVP parity.
 * Matches: PCI NIC, virtio-net, VM synth net.
 */
#ifndef FL_DRIVER_NET_H
#define FL_DRIVER_NET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fl_net_driver fl_net_driver_t;
struct fl_net_driver {
    int (*send)(fl_net_driver_t *drv, const void *buf, size_t len);
    int (*recv)(fl_net_driver_t *drv, void *buf, size_t max_len);
    void *impl;
};

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_NET_H */
