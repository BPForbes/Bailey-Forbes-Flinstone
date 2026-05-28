#include "net_http.h"

#include "net_socket.h"
#include "net_tcp.h"

#include <stdio.h>
#include <string.h>

fl_result_t fl_net_http_parse_status(const char *hdr, size_t hdr_len, int *status_out) {
    size_t i;
    int code = 0;

    if (!hdr || !status_out)
        return FL_RESULT_INVAL;
    if (hdr_len < 12u)
        return FL_RESULT_ERR;
    if (memcmp(hdr, "HTTP/", 5) != 0)
        return FL_RESULT_ERR;

    i = 5;
    while (i < hdr_len && hdr[i] != ' ')
        i++;
    if (i + 4u > hdr_len)
        return FL_RESULT_ERR;
    i++;
    if (hdr[i] < '0' || hdr[i] > '9' || hdr[i + 1] < '0' || hdr[i + 1] > '9' ||
        hdr[i + 2] < '0' || hdr[i + 2] > '9')
        return FL_RESULT_ERR;
    code = (hdr[i] - '0') * 100 + (hdr[i + 1] - '0') * 10 + (hdr[i + 2] - '0');
    *status_out = code;
    return FL_RESULT_OK;
}

fl_result_t fl_net_http_get(uint32_t peer_be, uint16_t port_host, const char *host_header,
                            const char *path, char *body, size_t body_cap, size_t *body_len,
                            unsigned timeout_ms) {
    char req[512];
    char rx[4096];
    size_t total = 0;
    size_t hdr_end = 0;
    int status = 0;
    int headers_done = 0;
    fl_net_sock_handle_t h = FL_NET_SOCK_INVALID;
    fl_result_t rc;

    if (!host_header || !path || !body || !body_len)
        return FL_RESULT_INVAL;
    if (port_host == 0u)
        return FL_RESULT_INVAL;

    {
        int req_len = snprintf(req, sizeof(req),
                               "GET %s HTTP/1.0\r\n"
                               "Host: %s\r\n"
                               "Connection: close\r\n"
                               "\r\n",
                               path, host_header);
        if (req_len < 0 || (size_t)req_len >= sizeof(req))
            return FL_RESULT_ERR;
    }

    rc = fl_net_tcp_stream_connect(peer_be, port_host, &h);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = fl_net_sock_send(h, req, strlen(req), &total);
    if (rc != FL_RESULT_OK || total != strlen(req)) {
        fl_net_sock_close(h);
        return FL_RESULT_ERR;
    }

    total = 0;
    while (total < sizeof(rx) - 1u) {
        size_t chunk = 0;

        rc = fl_net_sock_recv(h, rx + total, sizeof(rx) - 1u - total, &chunk, timeout_ms);
        if (rc == FL_RESULT_EOF)
            break;
        if (rc != FL_RESULT_OK) {
            fl_net_sock_close(h);
            return rc;
        }
        if (chunk == 0u)
            break;
        total += chunk;
        rx[total] = '\0';
        if (!headers_done && strstr(rx, "\r\n\r\n") != NULL)
            headers_done = 1;
    }
    fl_net_sock_close(h);
    if (total < 12u)
        return FL_RESULT_ERR;

    {
        const char *term = strstr(rx, "\r\n\r\n");
        if (!term)
            return FL_RESULT_ERR;
        hdr_end = (size_t)(term - rx) + 4u;
    }

    rc = fl_net_http_parse_status(rx, hdr_end, &status);
    if (rc != FL_RESULT_OK)
        return rc;
    if (status < 200 || status > 299)
        return FL_RESULT_ERR;

    if (hdr_end > total) {
        *body_len = 0;
        return FL_RESULT_OK;
    }
    if (total - hdr_end > body_cap)
        return FL_RESULT_ERR;
    memcpy(body, rx + hdr_end, total - hdr_end);
    *body_len = total - hdr_end;
    return FL_RESULT_OK;
}
