#ifndef NET_REQUIREMENTS_H
#define NET_REQUIREMENTS_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char endpoint[96];
    int ok;
    double rtt_ms;
    char detail[128];
} fl_net_requirements_report_t;

/** Probe **host**:**port** (same semantics as **fl_net_ping**). */
fl_result_t fl_net_probe_endpoint(const char *host, uint16_t port, unsigned timeout_ms,
                                  fl_net_requirements_report_t *out);

void fl_net_print_requirements_report(const fl_net_requirements_report_t *rep);

#ifdef __cplusplus
}
#endif

#endif /* NET_REQUIREMENTS_H */
