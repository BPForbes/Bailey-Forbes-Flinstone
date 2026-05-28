#ifndef NET_BAREMETAL_H
#define NET_BAREMETAL_H

#include "contract_result.h"
#include "fl/driver/net.h"

#include <stdint.h>

/** **P3-1** IEEE 802.3 lab netdev for **DRIVERS_BAREMETAL** (no Linux TAP/socket). */
void fl_net_baremetal_init(void);
void fl_net_baremetal_shutdown(void);

fl_net_driver_t *fl_net_netdev_lab(void);
int fl_net_baremetal_lab_is_up(void);

void fl_net_baremetal_lab_mac(uint8_t mac_out[6]);
uint32_t fl_net_baremetal_lab_ipv4_be(void);

/** Poll ARP TTL sweep from timer / idle hook (**#240** bare metal). */
uint64_t fl_net_baremetal_lab_stat_tx(void);
uint64_t fl_net_baremetal_lab_stat_rx(void);

void fl_net_baremetal_timer_poll(unsigned elapsed_ms);

#endif /* NET_BAREMETAL_H */
