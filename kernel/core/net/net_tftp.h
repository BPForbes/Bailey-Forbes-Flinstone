#ifndef NET_TFTP_H
#define NET_TFTP_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#ifndef FL_NET_TFTP_BLOCK_SIZE
#define FL_NET_TFTP_BLOCK_SIZE 512u
#endif

/** Build RRQ (opcode 1) into **buf**; returns length or 0. */
size_t fl_net_tftp_build_rrq(uint8_t *buf, size_t cap, const char *filename, const char *mode);

/**
 * Client read of **filename** from **server_be:port** (PX-12 / #260 subset).
 * Uses **fl_net_wire_send_udp_pkt** (egress). **buf** receives file data only.
 */
fl_result_t fl_net_tftp_read_file(uint32_t server_be, uint16_t port_host, const char *filename,
                                  uint8_t *buf, size_t cap, size_t *out_len,
                                  unsigned timeout_ms);

#endif /* NET_TFTP_H */
