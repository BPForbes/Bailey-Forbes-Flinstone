#ifndef NET_HTTP_H
#define NET_HTTP_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * HTTP/1.x **GET** over **fl_net_tcp_stream_connect** (PX-11 / #259).
 * Reads headers incrementally, then body via **Content-Length**, **chunked**
 * transfer coding, or until connection close when neither header is present.
 * Status must be **2xx**; body is capped by **body_cap**.
 */
fl_result_t fl_net_http_get(uint32_t peer_be, uint16_t port_host, const char *host_header,
                            const char *path, char *body, size_t body_cap, size_t *body_len,
                            unsigned timeout_ms);

/** Parse **HTTP/1.x** status code from the first line of **hdr** (e.g. `HTTP/1.1 200`). */
fl_result_t fl_net_http_parse_status(const char *hdr, size_t hdr_len, int *status_out);

/**
 * Parse **Content-Length** when present.
 * Returns **FL_RESULT_NOENT** when the header is absent.
 */
fl_result_t fl_net_http_parse_content_length(const char *hdr, size_t hdr_len, size_t *len_out);

/** Non-zero when **Transfer-Encoding: chunked** is present (case-insensitive). */
int fl_net_http_transfer_encoding_is_chunked(const char *hdr, size_t hdr_len);

#endif /* NET_HTTP_H */
