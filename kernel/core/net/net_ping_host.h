#ifndef NET_PING_HOST_H
#define NET_PING_HOST_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Probe host:port with in-tree IPv4/ICMP/TCP/DNS (hosted wire shim only at syscall edge).
 * port 0: ICMP echo (software loopback; Linux ICMP socket off-loopback).
 * port 1-65535: TCP SYN probe (software loopback RST+ACK; raw TCP off-loopback).
 * count applies to ICMP only (clamped 1..16); TCP performs one SYN exchange.
 */
fl_result_t fl_net_ping(const char *host, uint16_t port, unsigned count,
                        unsigned timeout_ms, double *out_rtt_ms, char *tcp_note,
                        size_t tcp_note_len);

/** Human-readable outcome for **fl_net_ping** (TCP vs ICMP, timeout/refused/permission). */
void fl_net_ping_format_result(const char *host, uint16_t port, fl_result_t rc,
                               double rtt_ms, char *buf, size_t buf_len,
                               const char *tcp_note);

#ifdef __cplusplus
}
#endif

#endif /* NET_PING_HOST_H */
