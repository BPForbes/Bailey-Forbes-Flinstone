#include "net_http.h"

#include "net_socket.h"
#include "net_tcp.h"

#include <stdio.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <strings.h>
#endif

#ifndef FL_NET_HTTP_RX_CHUNK
#define FL_NET_HTTP_RX_CHUNK 4096u
#endif

#ifndef FL_NET_HTTP_HDR_MAX
#define FL_NET_HTTP_HDR_MAX 8192u
#endif

#ifndef FL_NET_HTTP_BODY_MAX
#define FL_NET_HTTP_BODY_MAX (16u * 1024u * 1024u)
#endif

static int http_strncasecmp_local(const char *a, const char *b, size_t n) {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
    return strncasecmp(a, b, n);
#else
    size_t i;

    for (i = 0; i < n; i++) {
        int ca = (int)(unsigned char)a[i];
        int cb = (int)(unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z')
            cb += 'a' - 'A';
        if (ca != cb)
            return (ca < cb) ? -1 : 1;
        if (a[i] == '\0')
            break;
    }
    return 0;
#endif
}

static const char *http_find_crlfcrlf(const char *buf, size_t len, size_t *hdr_end_out) {
    size_t i;

    if (!buf || len < 4u)
        return NULL;
    for (i = 0; i + 3u < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            if (hdr_end_out)
                *hdr_end_out = i + 4u;
            return buf + i;
        }
    }
    return NULL;
}

static const char *http_header_value(const char *hdr, size_t hdr_len, const char *name,
                                     size_t *value_len_out) {
    size_t name_len;
    size_t pos = 0;

    if (!hdr || !name || !value_len_out || hdr_len == 0u)
        return NULL;
    name_len = strlen(name);

    while (pos < hdr_len) {
        size_t line_end = pos;
        const char *line;
        size_t line_len;
        size_t v;

        while (line_end < hdr_len && hdr[line_end] != '\r')
            line_end++;
        line = hdr + pos;
        line_len = line_end - pos;

        if (line_len > name_len && line[name_len] == ':' &&
            http_strncasecmp_local(line, name, name_len) == 0) {
            v = name_len + 1u;
            while (v < line_len && (line[v] == ' ' || line[v] == '\t'))
                v++;
            *value_len_out = line_len - v;
            return line + v;
        }

        if (line_end + 1u >= hdr_len)
            break;
        if (hdr[line_end] != '\r' || hdr[line_end + 1u] != '\n')
            break;
        pos = line_end + 2u;
    }
    return NULL;
}

static fl_result_t http_parse_size_decimal(const char *value, size_t value_len, size_t *out) {
    size_t n = 0;
    size_t i;

    if (!value || !out)
        return FL_RESULT_INVAL;
    if (value_len == 0u)
        return FL_RESULT_ERR;

    for (i = 0; i < value_len; i++) {
        char c = value[i];
        if (c < '0' || c > '9')
            return FL_RESULT_ERR;
        n = n * 10u + (size_t)(c - '0');
        if (n > FL_NET_HTTP_BODY_MAX)
            return FL_RESULT_ERR;
    }
    *out = n;
    return FL_RESULT_OK;
}

static fl_result_t http_parse_chunk_size_line(const char *line, size_t line_len, size_t *chunk_out) {
    size_t n = 0;
    size_t i;

    if (!line || !chunk_out)
        return FL_RESULT_INVAL;
    for (i = 0; i < line_len; i++) {
        char c = line[i];
        if (c == ';' || c == ' ' || c == '\t')
            break;
        if (c >= '0' && c <= '9')
            n = (n << 4) + (size_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            n = (n << 4) + (size_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            n = (n << 4) + (size_t)(c - 'A' + 10);
        else
            return FL_RESULT_ERR;
        if (n > FL_NET_HTTP_BODY_MAX)
            return FL_RESULT_ERR;
    }
    *chunk_out = n;
    return FL_RESULT_OK;
}

static fl_result_t http_body_append(char *body, size_t body_cap, size_t *body_len,
                                    const void *src, size_t src_len) {
    if (!body || !body_len || (!src && src_len > 0u))
        return FL_RESULT_INVAL;
    if (*body_len + src_len > body_cap)
        return FL_RESULT_ERR;
    if (src_len > 0u)
        memcpy(body + *body_len, src, src_len);
    *body_len += src_len;
    return FL_RESULT_OK;
}

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

fl_result_t fl_net_http_parse_content_length(const char *hdr, size_t hdr_len, size_t *len_out) {
    size_t value_len = 0;
    const char *value;

    if (!hdr || !len_out)
        return FL_RESULT_INVAL;
    value = http_header_value(hdr, hdr_len, "Content-Length", &value_len);
    if (!value)
        return FL_RESULT_NOENT;
    return http_parse_size_decimal(value, value_len, len_out);
}

int fl_net_http_transfer_encoding_is_chunked(const char *hdr, size_t hdr_len) {
    size_t value_len = 0;
    const char *value;

    if (!hdr || hdr_len == 0u)
        return 0;
    value = http_header_value(hdr, hdr_len, "Transfer-Encoding", &value_len);
    if (!value || value_len == 0u)
        return 0;
    if (value_len >= 7u && http_strncasecmp_local(value, "chunked", 7) == 0) {
        if (value_len == 7u)
            return 1;
        if (value[7] == ',' || value[7] == ' ' || value[7] == '\t')
            return 1;
    }
    return 0;
}

typedef struct {
    fl_net_sock_handle_t handle;
    unsigned timeout_ms;
    uint8_t chunk[FL_NET_HTTP_RX_CHUNK];
    size_t chunk_len;
    size_t chunk_off;
} http_stream_t;

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t off;
} http_pending_t;

static fl_result_t http_stream_refill(http_stream_t *s) {
    size_t got = 0;
    fl_result_t rc;

    if (!s)
        return FL_RESULT_INVAL;
    s->chunk_len = 0;
    s->chunk_off = 0;
    rc = fl_net_sock_recv(s->handle, s->chunk, sizeof(s->chunk), &got, s->timeout_ms);
    if (rc == FL_RESULT_EOF)
        return FL_RESULT_EOF;
    if (rc != FL_RESULT_OK)
        return rc;
    s->chunk_len = got;
    return FL_RESULT_OK;
}

static fl_result_t http_pending_refill(http_pending_t *p, http_stream_t *s) {
    fl_result_t rc;

    if (!p || !s)
        return FL_RESULT_INVAL;
    if (p->off < p->len)
        return FL_RESULT_OK;
    p->data = NULL;
    p->len = 0;
    p->off = 0;
    rc = http_stream_refill(s);
    if (rc == FL_RESULT_EOF)
        return FL_RESULT_EOF;
    if (rc != FL_RESULT_OK)
        return rc;
    if (s->chunk_len == 0u)
        return FL_RESULT_ERR;
    p->data = s->chunk;
    p->len = s->chunk_len;
    p->off = s->chunk_off;
    return FL_RESULT_OK;
}

static fl_result_t http_pending_read(http_pending_t *p, http_stream_t *s, uint8_t *dst,
                                     size_t need) {
    size_t copied = 0;

    if (!p || !s || !dst)
        return FL_RESULT_INVAL;
    if (need == 0u)
        return FL_RESULT_OK;

    while (copied < need) {
        size_t avail;
        size_t take;
        fl_result_t rc;

        if (p->off >= p->len) {
            rc = http_pending_refill(p, s);
            if (rc == FL_RESULT_EOF)
                return FL_RESULT_ERR;
            if (rc != FL_RESULT_OK)
                return rc;
        }
        avail = p->len - p->off;
        take = need - copied;
        if (take > avail)
            take = avail;
        memcpy(dst + copied, p->data + p->off, take);
        p->off += take;
        copied += take;
    }
    return FL_RESULT_OK;
}

static fl_result_t http_pending_read_line(http_pending_t *p, http_stream_t *s, char *line,
                                          size_t line_cap, size_t *line_len_out) {
    size_t n = 0;

    if (!p || !s || !line || !line_len_out || line_cap < 2u)
        return FL_RESULT_INVAL;

    while (1) {
        fl_result_t rc;

        if (p->off >= p->len) {
            rc = http_pending_refill(p, s);
            if (rc == FL_RESULT_EOF) {
                if (n == 0u)
                    return FL_RESULT_EOF;
                break;
            }
            if (rc != FL_RESULT_OK)
                return rc;
        }
        {
            uint8_t c = p->data[p->off++];
            if (c == '\r') {
                if (p->off >= p->len) {
                    rc = http_pending_refill(p, s);
                    if (rc != FL_RESULT_OK && rc != FL_RESULT_EOF)
                        return rc;
                }
                if (p->off < p->len && p->data[p->off] == '\n')
                    p->off++;
                break;
            }
            if (n + 1u >= line_cap)
                return FL_RESULT_ERR;
            line[n++] = (char)c;
        }
    }
    line[n] = '\0';
    *line_len_out = n;
    return FL_RESULT_OK;
}

static fl_result_t http_read_body_content_length(http_pending_t *pend, http_stream_t *stream,
                                                 size_t content_length, char *body,
                                                 size_t body_cap, size_t *body_len) {
    size_t copied = 0;
    fl_result_t rc;

    while (copied < content_length) {
        size_t need = content_length - copied;
        size_t avail = 0;

        if (pend->off < pend->len) {
            avail = pend->len - pend->off;
            if (avail > need)
                avail = need;
            rc = http_body_append(body, body_cap, body_len, pend->data + pend->off, avail);
            if (rc != FL_RESULT_OK)
                return rc;
            pend->off += avail;
            copied += avail;
            continue;
        }
        if (need > sizeof(stream->chunk))
            need = sizeof(stream->chunk);
        rc = http_pending_read(pend, stream, stream->chunk, need);
        if (rc != FL_RESULT_OK)
            return rc;
        rc = http_body_append(body, body_cap, body_len, stream->chunk, need);
        if (rc != FL_RESULT_OK)
            return rc;
        copied += need;
    }
    return FL_RESULT_OK;
}

static fl_result_t http_read_body_chunked(http_pending_t *pend, http_stream_t *stream,
                                          char *body, size_t body_cap, size_t *body_len) {
    fl_result_t rc;

    for (;;) {
        char line[32];
        size_t line_len = 0;
        size_t chunk_size = 0;

        rc = http_pending_read_line(pend, stream, line, sizeof(line), &line_len);
        if (rc != FL_RESULT_OK)
            return rc;
        rc = http_parse_chunk_size_line(line, line_len, &chunk_size);
        if (rc != FL_RESULT_OK)
            return rc;
        if (chunk_size == 0u)
            break;

        while (chunk_size > 0u) {
            size_t step;
            size_t avail = 0;

            if (pend->off < pend->len) {
                avail = pend->len - pend->off;
                if (avail > chunk_size)
                    avail = chunk_size;
                rc = http_body_append(body, body_cap, body_len, pend->data + pend->off, avail);
                if (rc != FL_RESULT_OK)
                    return rc;
                pend->off += avail;
                chunk_size -= avail;
                continue;
            }
            step = chunk_size;
            if (step > sizeof(stream->chunk))
                step = sizeof(stream->chunk);
            rc = http_pending_read(pend, stream, stream->chunk, step);
            if (rc != FL_RESULT_OK)
                return rc;
            rc = http_body_append(body, body_cap, body_len, stream->chunk, step);
            if (rc != FL_RESULT_OK)
                return rc;
            chunk_size -= step;
        }
        {
            uint8_t crlf[2];
            rc = http_pending_read(pend, stream, crlf, 2);
            if (rc != FL_RESULT_OK)
                return rc;
            if (crlf[0] != '\r' || crlf[1] != '\n')
                return FL_RESULT_ERR;
        }
    }

    for (;;) {
        char line[256];
        size_t line_len = 0;

        rc = http_pending_read_line(pend, stream, line, sizeof(line), &line_len);
        if (rc == FL_RESULT_EOF)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_OK)
            return rc;
        if (line_len == 0u)
            break;
    }
    return FL_RESULT_OK;
}

static fl_result_t http_read_body_until_close(http_pending_t *pend, http_stream_t *stream,
                                              char *body, size_t body_cap, size_t *body_len) {
    fl_result_t rc;

    for (;;) {
        size_t avail;

        if (pend->off < pend->len) {
            avail = pend->len - pend->off;
            rc = http_body_append(body, body_cap, body_len, pend->data + pend->off, avail);
            if (rc != FL_RESULT_OK)
                return rc;
            pend->off = pend->len;
            continue;
        }
        rc = http_pending_refill(pend, stream);
        if (rc == FL_RESULT_EOF)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_OK)
            return rc;
    }
}

fl_result_t fl_net_http_get(uint32_t peer_be, uint16_t port_host, const char *host_header,
                            const char *path, char *body, size_t body_cap, size_t *body_len,
                            unsigned timeout_ms) {
    char req[512];
    uint8_t hdr_acc[FL_NET_HTTP_HDR_MAX];
    size_t hdr_total = 0;
    size_t hdr_end = 0;
    int status = 0;
    int chunked = 0;
    size_t content_length = 0;
    int have_content_length = 0;
    http_stream_t stream;
    http_pending_t pend;
    fl_net_sock_handle_t h = FL_NET_SOCK_INVALID;
    fl_result_t rc;

    if (!host_header || !path || !body || !body_len)
        return FL_RESULT_INVAL;
    if (port_host == 0u)
        return FL_RESULT_INVAL;

    *body_len = 0;
    memset(&stream, 0, sizeof(stream));
    memset(&pend, 0, sizeof(pend));
    stream.timeout_ms = timeout_ms;

    {
        int req_len = snprintf(req, sizeof(req),
                               "GET %s HTTP/1.1\r\n"
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

    stream.handle = h;

    {
        size_t sent = 0;
        size_t req_len = strlen(req);

        rc = fl_net_sock_send(h, req, req_len, &sent);
        if (rc != FL_RESULT_OK || sent != req_len) {
            fl_net_sock_close(h);
            return FL_RESULT_ERR;
        }
    }

    while (!http_find_crlfcrlf((const char *)hdr_acc, hdr_total, &hdr_end)) {
        size_t got = 0;
        size_t space;

        if (hdr_total >= FL_NET_HTTP_HDR_MAX)
            break;
        space = FL_NET_HTTP_HDR_MAX - hdr_total;
        if (space > FL_NET_HTTP_RX_CHUNK)
            space = FL_NET_HTTP_RX_CHUNK;
        rc = fl_net_sock_recv(h, hdr_acc + hdr_total, space, &got, timeout_ms);
        if (rc == FL_RESULT_EOF)
            break;
        if (rc != FL_RESULT_OK) {
            fl_net_sock_close(h);
            return rc;
        }
        if (got == 0u)
            break;
        hdr_total += got;
    }

    if (!http_find_crlfcrlf((const char *)hdr_acc, hdr_total, &hdr_end)) {
        fl_net_sock_close(h);
        return FL_RESULT_ERR;
    }

    rc = fl_net_http_parse_status((const char *)hdr_acc, hdr_end, &status);
    if (rc != FL_RESULT_OK) {
        fl_net_sock_close(h);
        return rc;
    }
    if (status < 200 || status > 299) {
        fl_net_sock_close(h);
        return FL_RESULT_ERR;
    }

    chunked = fl_net_http_transfer_encoding_is_chunked((const char *)hdr_acc, hdr_end);
    rc = fl_net_http_parse_content_length((const char *)hdr_acc, hdr_end, &content_length);
    have_content_length = (rc == FL_RESULT_OK);
    if (rc != FL_RESULT_OK && rc != FL_RESULT_NOENT) {
        fl_net_sock_close(h);
        return rc;
    }

    pend.data = hdr_acc + hdr_end;
    pend.len = hdr_total - hdr_end;
    pend.off = 0;
    stream.chunk_len = 0;
    stream.chunk_off = 0;

    if (chunked) {
        rc = http_read_body_chunked(&pend, &stream, body, body_cap, body_len);
    } else if (have_content_length) {
        rc = http_read_body_content_length(&pend, &stream, content_length, body, body_cap,
                                           body_len);
    } else {
        rc = http_read_body_until_close(&pend, &stream, body, body_cap, body_len);
    }

    fl_net_sock_close(h);
    return rc;
}
