#include "cmd_decl.h"
#include "cmd_batch.h"
#include "net_requirements.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_u16(const char *s, uint16_t *out) {
    char *end = NULL;
    long v;

    if (!s || !out)
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || (end && *end != '\0'))
        return -1;
    if (v < 0 || v > 65535)
        return -1;
    *out = (uint16_t)v;
    return 0;
}

static int run_requirements(const char *host, uint16_t port) {
    fl_net_requirements_report_t rep;
    fl_result_t rc;

    printf("check requirements (P3 / PRE 4.2.0):\n");

    rc = fl_net_probe_endpoint(host, port, 4000u, &rep);
    if (rc != FL_RESULT_OK) {
        printf("  probe: error (%d)\n", (int)rc);
        return 1;
    }
    fl_net_print_requirements_report(&rep);
    return rep.ok ? 0 : 1;
}

int cmd_check_run(int argc, char **argv) {
    const char *host;
    uint16_t port;

    if (argc < 4 || strcmp(argv[1], "requirements") != 0) {
        fprintf(stderr,
                "check: usage: check requirements <host> <port>\n"
                "       port 0 = ICMP echo; port 1-65535 = TCP connect to that port\n"
                "       host may be a DNS name or IPv4 address (resolved live)\n");
        return 1;
    }

    host = argv[2];
    if (parse_u16(argv[3], &port) != 0) {
        fprintf(stderr, "check: invalid port '%s'\n", argv[3]);
        return 1;
    }

    return run_requirements(host, port);
}

int cmd_check_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    if (i + 1 < argc && !strcmp(argv[i + 1], "requirements"))
        used++;
    if (i + 2 < argc && !strcmp(argv[i + 1], "requirements"))
        used++;
    if (i + 3 < argc && !strcmp(argv[i + 1], "requirements"))
        used++;
    (void)argc;
    return used;
}
