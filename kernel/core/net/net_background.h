#ifndef NET_BACKGROUND_H
#define NET_BACKGROUND_H

#include "contract_p3_background.h"
#include "contract_p3_packet.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

void fl_net_background_init(void);
void fl_net_background_shutdown(void);

/** Called from **fl_bg_jobs_tick** and optionally from driver RX paths. */
void fl_net_background_tick(unsigned max_items);

/** Schedule ARP cache TTL sweep (**#240**). */
fl_result_t fl_net_background_arp_tick_kick(void);

/**
 * Network task backend (P3-13 groundwork):
 * Connected-user delivery uses in-kernel message queues (msgq_t) so a future
 * "message" component can forward packet payloads between users without
 * depending on TCP/UDP FSMs being complete yet.
 *
 * Implementation detail: payload is copied from fl_net_packet_t->l4 into a
 * fixed-size inbox message; message bytes ride through asm-backed copies
 * (msgq_send / msgq_receive).
 */
#define FL_NET_TASK_BACKEND_MAX_USERS 16u
#define FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX 2046u

fl_result_t fl_net_task_backend_user_open(unsigned slot, unsigned max_inbox_messages);
void fl_net_task_backend_user_close(unsigned slot);

/** Enqueue parsed packet L4 payload into dst_user's inbox. */
fl_result_t fl_net_task_backend_send_packet(unsigned dst_slot, const fl_net_packet_t *pkt);

/**
 * Dequeue one packet L4 payload from slot's inbox.
 * Returns FL_RESULT_TIMEDOUT when the inbox is empty (non-blocking).
 */
fl_result_t fl_net_task_backend_recv_packet(unsigned src_slot, uint8_t *out, size_t cap,
                                              size_t *out_len);

#endif /* NET_BACKGROUND_H */
