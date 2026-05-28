#include "net_tls_hosted.h"

#include "contract_p3_tls_hosted.h"

#include <string.h>

#if defined(__has_include)
#if __has_include(<openssl/ssl.h>)
#define FL_NET_TLS_HAVE_OPENSSL 1
#endif
#endif

#if defined(FL_NET_TLS_HAVE_OPENSSL)
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

size_t fl_net_tls_hosted_max_plaintext_record(void) {
    return (size_t)FL_NET_TLS_MAX_PLAINTEXT_RECORD;
}

int fl_net_tls_hosted_openssl_available(void) {
#if defined(FL_NET_TLS_HAVE_OPENSSL)
    return 1;
#else
    return 0;
#endif
}

#if defined(FL_NET_TLS_HAVE_OPENSSL)

static SSL_CTX *s_tls_client_ctx;

static SSL_CTX *tls_client_ctx(void) {
    if (!s_tls_client_ctx) {
        const SSL_METHOD *method = TLS_client_method();

        s_tls_client_ctx = SSL_CTX_new(method);
        if (s_tls_client_ctx) {
            (void)SSL_CTX_set_min_proto_version(s_tls_client_ctx, TLS1_2_VERSION);
            if (SSL_CTX_set_default_verify_paths(s_tls_client_ctx) != 1) {
                SSL_CTX_free(s_tls_client_ctx);
                s_tls_client_ctx = NULL;
            } else {
                SSL_CTX_set_verify(s_tls_client_ctx, SSL_VERIFY_PEER, NULL);
            }
        }
    }
    return s_tls_client_ctx;
}

fl_result_t fl_net_tls_hosted_client_connect(int tcp_fd, const char *sni_host,
                                             fl_net_tls_session_t *session_out) {
    SSL_CTX *ctx;
    SSL *ssl;

    if (!session_out || tcp_fd < 0)
        return FL_RESULT_INVAL;
    *session_out = FL_NET_TLS_SESSION_INVALID;

    ctx = tls_client_ctx();
    if (!ctx)
        return FL_RESULT_ERR;

    ssl = SSL_new(ctx);
    if (!ssl)
        return FL_RESULT_ERR;
    if (SSL_set_fd(ssl, tcp_fd) != 1) {
        SSL_free(ssl);
        return FL_RESULT_ERR;
    }
    if (sni_host && sni_host[0]) {
        if (SSL_set_tlsext_host_name(ssl, sni_host) != 1 || SSL_set1_host(ssl, sni_host) != 1) {
            SSL_free(ssl);
            return FL_RESULT_ERR;
        }
    }
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        return FL_RESULT_ERR;
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        SSL_free(ssl);
        return FL_RESULT_ERR;
    }
    *session_out = (fl_net_tls_session_t)(uintptr_t)ssl;
    return FL_RESULT_OK;
}

fl_result_t fl_net_tls_hosted_read(fl_net_tls_session_t session, void *buf, size_t cap,
                                   size_t *got) {
    SSL *ssl = (SSL *)(uintptr_t)session;
    int n;

    if (!ssl || !buf || !got || cap == 0u)
        return FL_RESULT_INVAL;
    n = SSL_read(ssl, buf, (int)cap);
    if (n > 0) {
        *got = (size_t)n;
        return FL_RESULT_OK;
    }
    if (n <= 0) {
        int err = SSL_get_error(ssl, n);
        if (err == SSL_ERROR_ZERO_RETURN) {
            *got = 0;
            return FL_RESULT_EOF;
        }
    }
    return FL_RESULT_ERR;
}

fl_result_t fl_net_tls_hosted_write(fl_net_tls_session_t session, const void *buf, size_t len,
                                    size_t *sent) {
    SSL *ssl = (SSL *)(uintptr_t)session;
    int n;

    if (!ssl || !buf || !sent || len == 0u)
        return FL_RESULT_INVAL;
    n = SSL_write(ssl, buf, (int)len);
    if (n <= 0)
        return FL_RESULT_ERR;
    *sent = (size_t)n;
    return FL_RESULT_OK;
}

void fl_net_tls_hosted_close(fl_net_tls_session_t session) {
    SSL *ssl = (SSL *)(uintptr_t)session;

    if (!ssl)
        return;
    (void)SSL_shutdown(ssl);
    SSL_free(ssl);
}

#else /* !FL_NET_TLS_HAVE_OPENSSL */

fl_result_t fl_net_tls_hosted_client_connect(int tcp_fd, const char *sni_host,
                                             fl_net_tls_session_t *session_out) {
    (void)tcp_fd;
    (void)sni_host;
    if (session_out)
        *session_out = FL_NET_TLS_SESSION_INVALID;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_tls_hosted_read(fl_net_tls_session_t session, void *buf, size_t cap,
                                   size_t *got) {
    (void)session;
    (void)buf;
    (void)cap;
    if (got)
        *got = 0;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_tls_hosted_write(fl_net_tls_session_t session, const void *buf, size_t len,
                                    size_t *sent) {
    (void)session;
    (void)buf;
    (void)len;
    if (sent)
        *sent = 0;
    return FL_RESULT_NOSYS;
}

void fl_net_tls_hosted_close(fl_net_tls_session_t session) {
    (void)session;
}

#endif /* FL_NET_TLS_HAVE_OPENSSL */
