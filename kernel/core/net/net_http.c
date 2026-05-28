#include "net_http.h"

#include "net_socket.h"
#include "net_tcp.h"

#include <stdio.h>
#include <string.h>

fl_result_t fl_net_http_parse_status(const char *hdr, size_t hdr_len, int *status_out) {
    int code = 0;

    if (!hdr || !status_out)
        return FL_RESULT_INVAL;
    if (hdr_len < 12u)
        return FL_RESULT_ERR;

    if (strncmp(hdr, "HTTP/", 5) != 0)
        return FL_RESULT_ERR;
    if (sscanf(hdr, "HTTP/%*d.%*d %d", &code) != 1)
        return FL_RESULT_ERR;
    *status_out = code;
    return FL_RESULT_OK;
}

fl_result_t fl_net_http_get(uint32_t peer_be, uint16_t port_host, const char *host_header,
                            const char *path, char *body, size_t body_cap, size_t *body_len,
                            unsigned timeout_ms) {
    char req[512];
    char rx[4096];
    size_t got = 0;
    size_t hdr_end = 0;
    int status = 0;
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

    rc = fl_net_sock_send(h, req, strlen(req), &got);
    if (rc != FL_RESULT_OK || got != strlen(req)) {
        fl_net_sock_close(h);
        return FL_RESULT_ERR;
    }

    got = 0;
    rc = fl_net_sock_recv(h, rx, sizeof(rx) - 1u, &got, timeout_ms);
    fl_net_sock_close(h);
    if (rc != FL_RESULT_OK)
        return rc;
    if (got < 12u)
        return FL_RESULT_ERR;
    rx[got] = '\0';

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

    if (hdr_end > got) {
        *body_len = 0;
        return FL_RESULT_OK;
    }
    if (got - hdr_end > body_cap)
        return FL_RESULT_ERR;
    memcpy(body, rx + hdr_end, got - hdr_end);
    *body_len = got - hdr_end;
    return FL_RESULT_OK;
}
