#ifndef NET_TLS_HOSTED_H
#define NET_TLS_HOSTED_H

#include <stddef.h>

/**
 * Hosted **P3-9** boundary: sizing hook only. Record encryption stays in userland
 * libraries (mbedTLS/OpenSSL) per **contract_p3_tls_hosted.h**.
 */
size_t fl_net_tls_hosted_max_plaintext_record(void);

#endif /* NET_TLS_HOSTED_H */
