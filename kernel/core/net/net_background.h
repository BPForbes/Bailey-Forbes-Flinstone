#ifndef NET_BACKGROUND_H
#define NET_BACKGROUND_H

#include "contract_result.h"

/** P3-14 — network stack background work (RX dequeue, TCP timers, ARP TTL). */

void fl_net_background_init(void);
void fl_net_background_shutdown(void);

/** Called from **fl_bg_jobs_tick** and optionally from driver RX paths. */
void fl_net_background_tick(unsigned max_items);

/** Schedule ARP cache TTL sweep (**#240**). */
fl_result_t fl_net_background_arp_tick_kick(void);

#endif /* NET_BACKGROUND_H */
