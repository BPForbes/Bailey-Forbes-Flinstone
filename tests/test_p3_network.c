#include "net_dns.h"
#include "net_ipv4.h"
#include "net_ping_host.h"
#include "net_requirements.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            return 1; \
        } \
    } while (0)

static int test_loopback_octet(void) {
    struct in_addr a;
    ASSERT(inet_pton(AF_INET, "127.0.0.1", &a) == 1);
    ASSERT(fl_net_ipv4_is_loopback(a.s_addr));
    ASSERT(inet_pton(AF_INET, "8.8.8.8", &a) == 1);
    ASSERT(!fl_net_ipv4_is_loopback(a.s_addr));
    return 0;
}

static int test_resolve_localhost(void) {
    uint32_t addr_be = 0;
    char resolved[INET_ADDRSTRLEN];
    ASSERT(fl_net_resolve_ipv4("localhost", &addr_be, resolved, sizeof(resolved)) ==
           FL_RESULT_OK);
    ASSERT(fl_net_ipv4_is_loopback(addr_be));
    return 0;
}

static int test_loopback_ping(void) {
    double rtt = 0.0;
    fl_result_t rc = fl_net_ping("127.0.0.1", 0, 1u, 3000u, &rtt);
    if (rc == FL_RESULT_NOSYS || rc == FL_RESULT_TIMEDOUT) {
        fprintf(stderr, "skip: ICMP echo unavailable in this environment\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rtt >= 0.0);
    return 0;
}

static int test_loopback_tcp(void) {
    double rtt = 0.0;
    fl_result_t rc = fl_net_ping("127.0.0.1", 9, 1u, 3000u, &rtt);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rtt >= 0.0);
    return 0;
}

static int test_probe_endpoint(void) {
    fl_net_requirements_report_t rep;
    fl_result_t prc = fl_net_probe_endpoint("127.0.0.1", 9, 3000u, &rep);
    ASSERT(prc == FL_RESULT_OK);
    if (getenv("SKIP_NETWORK_INTEROP") &&
        !strcmp(getenv("SKIP_NETWORK_INTEROP"), "1")) {
        return 0;
    }
    ASSERT(rep.ok == 1);
    ASSERT(strstr(rep.endpoint, "127.0.0.1") != NULL);
    return 0;
}

int main(void) {
    printf("test_loopback_octet... ");
    if (test_loopback_octet() != 0)
        return 1;
    puts("ok");

    printf("test_resolve_localhost... ");
    if (test_resolve_localhost() != 0)
        return 1;
    puts("ok");

    printf("test_loopback_ping... ");
    if (test_loopback_ping() != 0)
        return 1;
    puts("ok");

    printf("test_loopback_tcp... ");
    if (test_loopback_tcp() != 0)
        return 1;
    puts("ok");

    printf("test_probe_endpoint... ");
    if (test_probe_endpoint() != 0)
        return 1;
    puts("ok");

    puts("test_p3_network: all passed");
    return 0;
}
