#include "cmd_decl.h"
#include "cmd_batch.h"
#include "net_requirements.h"

#include <stdio.h>
#include <string.h>

static int run_requirements(int probe_internet) {
    fl_net_requirements_report_t rep;
    fl_result_t rc;
    int fail = 0;

    printf("check requirements (P3 / PRE 4.2.0):\n");
    printf("  p3_loopback: 127.0.0.0/8 ICMP + optional internet TCP\n");

    rc = fl_net_probe_requirements(probe_internet, &rep);
    if (rc != FL_RESULT_OK) {
        printf("  probe: error (%d)\n", (int)rc);
        return 1;
    }
    fl_net_print_requirements_report(&rep);

    if (!rep.loopback_icmp_ok)
        fail = 1;
    if (probe_internet && !rep.internet_skipped && !rep.internet_tcp_ok)
        fail = 1;

    return fail ? 1 : 0;
}

int cmd_check_run(int argc, char **argv) {
    int probe_internet = 1;

    if (argc >= 2 && !strcmp(argv[1], "requirements")) {
        if (argc >= 3 && !strcmp(argv[2], "--no-internet"))
            probe_internet = 0;
        return run_requirements(probe_internet);
    }

    fprintf(stderr, "check: usage: check requirements [--no-internet]\n");
    return 1;
}

int cmd_check_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    if (i + 1 < argc && !strcmp(argv[i + 1], "requirements"))
        used++;
    if (i + 2 < argc && !strcmp(argv[i + 1], "requirements") &&
        !strcmp(argv[i + 2], "--no-internet"))
        used++;
    (void)argc;
    return used;
}
