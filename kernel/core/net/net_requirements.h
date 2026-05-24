#ifndef NET_REQUIREMENTS_H
#define NET_REQUIREMENTS_H

#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int loopback_icmp_ok;
    double loopback_rtt_ms;
    int internet_tcp_ok;
    int internet_skipped;
    const char *internet_detail;
} fl_net_requirements_report_t;

/**
 * Probe loopback ICMP (**127.0.0.1**) and optional internet TCP (**1.1.1.1:443**).
 * When **probe_internet** is 0, internet fields are marked skipped.
 */
fl_result_t fl_net_probe_requirements(int probe_internet,
                                      fl_net_requirements_report_t *out);

/** Print human-readable lines to **stdout** (for **check requirements**). */
void fl_net_print_requirements_report(const fl_net_requirements_report_t *rep);

#ifdef __cplusplus
}
#endif

#endif /* NET_REQUIREMENTS_H */
