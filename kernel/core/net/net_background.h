#ifndef NET_BACKGROUND_H
#define NET_BACKGROUND_H

#include "contract_p3_background.h"
#include "contract_p3_packet.h"
#include "contract_p3_socket.h"
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
 * Network task backend — blended **socket** + **ARP** distribution hub.
 *
 * **Model:** Socket(A,B) = (IP_A, Port_A, IP_B, Port_B). Send path:
 *
 *   Data → UDP segment → IPv4 → ARP(peer IP) → Ethernet frame → netdev
 *
 * **Client → server:** `fl_net_task_backend_socket_send` toward hub (**peer** = server).
 * **Server → clients:** `fl_net_task_backend_server_relay_to_clients` fans out inboxes
 * and, when peers are bound, transmits Socket(hub, C_i) with **ARP(IP_Ci)** per client.
 *
 * TODO: P3-13 — full TCP/socket shim, wire RX demux, `server` shell command.
 */
#define FL_NET_TASK_BACKEND_MAX_USERS 16u
#define FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX 2046u

/** Lab default server UDP port (P3-13 may override). */
#define FL_NET_TASK_BACKEND_WIRE_SERVER_PORT 7777u

/** Default ARP resolve wait for backend wire xmit (milliseconds). */
#define FL_NET_TASK_BACKEND_ARP_TIMEOUT_MS 500u

fl_result_t fl_net_task_backend_user_open(unsigned slot, unsigned max_inbox_messages);
void fl_net_task_backend_user_close(unsigned slot);

/**
 * Bind **peer** IPv4/port for **slot** (client address for server→client ARP wire relay).
 * **port_host** is host byte order (1–65535).
 */
fl_result_t fl_net_task_backend_peer_bind(unsigned slot, uint32_t peer_ip_be, uint16_t port_host);

/** Hub identity for server relay wire path (local IP/port on the server). */
fl_result_t fl_net_task_backend_hub_bind(uint32_t hub_ip_be, uint16_t hub_port_host);

/** Enqueue parsed packet L4 payload into dst_user's inbox. */
fl_result_t fl_net_task_backend_send_packet(unsigned dst_slot, const fl_net_packet_t *pkt);

/**
 * Dequeue one packet L4 payload from slot's inbox.
 * Returns FL_RESULT_TIMEDOUT when the inbox is empty (non-blocking).
 */
fl_result_t fl_net_task_backend_recv_packet(unsigned src_slot, uint8_t *out, size_t cap,
                                              size_t *out_len);

/**
 * Socket egress: build UDP from **endpoint** + payload, transmit via routed netdev + ARP.
 * On **FL_RESULT_NOENT**, falls back to hosted **fl_net_wire_xmit_udp_pkt** (sendto only;
 * propagates send failures instead of masking them as success).
 */
fl_result_t fl_net_task_backend_socket_send(const fl_net_socket_endpoint_t *endpoint,
                                            const uint8_t *payload, size_t payload_len);

/**
 * Client → server convenience wrapper (builds **fl_net_socket_endpoint_t**).
 */
fl_result_t fl_net_task_backend_client_wire_send(uint32_t server_addr_be, uint16_t client_sport,
                                                 uint16_t server_port,
                                                 const fl_net_packet_t *pkt);

/**
 * Server hub: relay to other clients (inbox + ARP/socket wire when peers are bound).
 */
fl_result_t fl_net_task_backend_server_relay_to_clients(unsigned src_slot,
                                                        const fl_net_packet_t *pkt);

/**
 * Server ingress after wire RX parse. TODO: P3-13 demux calls this.
 */
fl_result_t fl_net_task_backend_server_ingress(unsigned from_client_slot,
                                               const fl_net_packet_t *pkt);

#endif /* NET_BACKGROUND_H */
