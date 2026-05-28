#ifndef NET_TLS_HOSTED_H
#define NET_TLS_HOSTED_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Hosted **P3-9** boundary: record sizing plus optional OpenSSL client bridge
 * (**#252**). Ciphertext stays in userland libraries per **contract_p3_tls_hosted.h**.
 */
size_t fl_net_tls_hosted_max_plaintext_record(void);

/** Non-zero when **libssl** bridge was compiled in (**FL_NET_TLS_HAVE_OPENSSL**). */
int fl_net_tls_hosted_openssl_available(void);

typedef uintptr_t fl_net_tls_session_t;

#ifndef FL_NET_TLS_SESSION_INVALID
#define FL_NET_TLS_SESSION_INVALID ((fl_net_tls_session_t)0)
#endif

/**
 * Client TLS over an already-connected TCP **host** socket fd (hosted only).
 * **sni_host** may be NULL to skip SNI. Returns **FL_RESULT_NOSYS** when OpenSSL
 * is not linked.
 */
fl_result_t fl_net_tls_hosted_client_connect(int tcp_fd, const char *sni_host,
                                             fl_net_tls_session_t *session_out);

fl_result_t fl_net_tls_hosted_read(fl_net_tls_session_t session, void *buf, size_t cap,
                                 size_t *got);
fl_result_t fl_net_tls_hosted_write(fl_net_tls_session_t session, const void *buf, size_t len,
                                    size_t *sent);
void fl_net_tls_hosted_close(fl_net_tls_session_t session);

#endif /* NET_TLS_HOSTED_H */
