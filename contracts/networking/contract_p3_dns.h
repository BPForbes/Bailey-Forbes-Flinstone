/**
 * **P3-8 — DNS client** (module contract, normative).
 *
 * **Distribution:** DNS messages are typically carried in **UDP** datagrams (**P3-6**)
 * bounded by **FL_NET_DNS_UDP_DEFAULT_MAX** octets for this contract’s initial subset
 * (**RFC 1035**); **TCP** fallback is a separate explicit extension when added.
 *
 * **Timeouts / retries:** finite caps are mandatory on every resolver call surface.
 */
#ifndef FL_CONTRACT_P3_DNS_H
#define FL_CONTRACT_P3_DNS_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_8_DNS_CONTRACT_DEFINED 1

#ifndef FL_NET_DNS_UDP_DEFAULT_MAX
#define FL_NET_DNS_UDP_DEFAULT_MAX 512u
#endif

#endif /* FL_CONTRACT_P3_DNS_H */
