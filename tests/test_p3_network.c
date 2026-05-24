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

static int test_loopback_ping(void) {
    double rtt = 0.0;
    fl_result_t rc = fl_net_ping_ipv4("127.0.0.1", 1u, 3000u, &rtt);
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: ICMP ping socket not available\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rtt >= 0.0);
    return 0;
}

static int test_requirements_loopback(void) {
    fl_net_requirements_report_t rep;
    ASSERT(fl_net_probe_requirements(0, &rep) == FL_RESULT_OK);
    if (getenv("SKIP_NETWORK_INTEROP") &&
        !strcmp(getenv("SKIP_NETWORK_INTEROP"), "1")) {
        return 0;
    }
    ASSERT(rep.loopback_icmp_ok == 1);
    return 0;
}

int main(void) {
    printf("test_loopback_octet... ");
    if (test_loopback_octet() != 0)
        return 1;
    puts("ok");

    printf("test_loopback_ping... ");
    if (test_loopback_ping() != 0)
        return 1;
    puts("ok");

    printf("test_requirements_loopback... ");
    if (test_requirements_loopback() != 0)
        return 1;
    puts("ok");

    puts("test_p3_network: all passed");
    return 0;
}
