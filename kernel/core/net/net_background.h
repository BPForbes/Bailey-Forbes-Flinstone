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
 * Network task backend — packet distribution hub (client → server → clients).
 *
 * **Flow (normative for P3-13 message/server work):**
 * 1. **Client egress:** `fl_net_task_backend_client_wire_send` copies the parsed
 *    packet L4 payload onto the wire (UDP today) toward the server host.
 * 2. **Server ingress:** TODO P3-13 — wire RX demux parses frames and calls
 *    `fl_net_task_backend_server_ingress` (not wired to netdev RX in this PR).
 * 3. **Server relay:** `fl_net_task_backend_server_relay_to_clients` enqueues the
 *    packet L4 payload into every other connected client inbox (msgq_t).
 *
 * Payload bytes use asm-backed copies (`fl_net_packet_copy_l4`, msgq I/O).
 * Full `server` shell / TCP hub lives in P3-13 (`docs/P3_13_CHAT_SERVER.md`).
 */
#define FL_NET_TASK_BACKEND_MAX_USERS 16u
#define FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX 2046u

/** Lab default server UDP port for backend wire egress (P3-13 may override). */
#define FL_NET_TASK_BACKEND_WIRE_SERVER_PORT 7777u

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

/**
 * Client path: send packet L4 payload to **server_addr_be**:**server_port** on the wire.
 * One-way egress (recv timeout is treated as success when the datagram was sent).
 */
fl_result_t fl_net_task_backend_client_wire_send(uint32_t server_addr_be, uint16_t client_sport,
                                                 uint16_t server_port,
                                                 const fl_net_packet_t *pkt);

/**
 * Server path: relay **pkt** to every open client inbox except **src_slot**.
 * Returns **FL_RESULT_OK** when at least one client received the payload.
 */
fl_result_t fl_net_task_backend_server_relay_to_clients(unsigned src_slot,
                                                        const fl_net_packet_t *pkt);

/**
 * Server path: accept an inbound packet at the hub and relay to other clients.
 * TODO: P3-13 wire RX demux calls this after parsing server-bound frames.
 */
fl_result_t fl_net_task_backend_server_ingress(unsigned from_client_slot,
                                               const fl_net_packet_t *pkt);

#endif /* NET_BACKGROUND_H */
