#ifndef NET_HTTP_H
#define NET_HTTP_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Minimal HTTP/1.x **GET** over **fl_net_tcp_stream_connect** (PX-11 / #259).
 * Fetches response body after header terminator; status must be **2xx**.
 */
fl_result_t fl_net_http_get(uint32_t peer_be, uint16_t port_host, const char *host_header,
                            const char *path, char *body, size_t body_cap, size_t *body_len,
                            unsigned timeout_ms);

/** Parse **HTTP/1.x** status code from the first line of **hdr** (e.g. `HTTP/1.1 200`). */
fl_result_t fl_net_http_parse_status(const char *hdr, size_t hdr_len, int *status_out);

#endif /* NET_HTTP_H */
