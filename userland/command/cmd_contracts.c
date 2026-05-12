#include "cmd_decl.h"
#include "fl/contract.h"
#include "fl/history_record.h"
#include <stdio.h>
#include <string.h>

static void print_contract_help(void) {
    printf(
        "Usage: contracts [summary|json] [--help]\n"
        "\n"
        "Print the public subsystem contract bundle (headers under fl/contract*.h).\n"
        "Designed for automation: no prompts; use `json` for one-line parsing.\n"
        "\n"
        "Options:\n"
        "  summary   Human-readable listing (default)\n"
        "  json      Single-line JSON: bundle rev, result codes, surface ids\n"
        "  --help    Show this message\n"
        "\n"
        "Examples:\n"
        "  contracts\n"
        "  contracts summary\n"
        "  contracts json\n");
}

static fl_authz_decision_t demo_authz_always_allow(unsigned op, void *ctx) {
    (void)op;
    (void)ctx;
    return FL_AUTHZ_ALLOW;
}

static int print_summary(void) {
    static const char *surface_names[] = {
        "DRIVER_OPS", "NETDEV", "LOG_SINK", "AUTHZ",
    };

    printf("contracts: bundle rev %d\n", FL_CONTRACT_BUNDLE_REV);
    printf("  fl_result_t: OK=%d ERR=%d INVAL=%d NOSYS=%d\n",
           (int)FL_RESULT_OK, (int)FL_RESULT_ERR, (int)FL_RESULT_INVAL, (int)FL_RESULT_NOSYS);
    printf("  surfaces (%zu):\n", sizeof(surface_names) / sizeof(surface_names[0]));
    for (unsigned i = 0; i < sizeof(surface_names) / sizeof(surface_names[0]); i++)
        printf("    %u %s\n", i, surface_names[i]);
    printf("  headers: fl/contract.h (+ driver.h, net.h, contract_*)\n");
    printf("  demo fl_authz_check_fn: returns ALLOW (no policy wired)\n");

    fl_authz_check_fn check = demo_authz_always_allow;
    printf("  demo authz(0,NULL) => %d\n", (int)check(0, NULL));
    return 0;
}

static int print_json(void) {
    printf("{\"bundle_rev\":%d,\"fl_result_ok\":%d,\"fl_result_err\":%d,"
           "\"surfaces\":[\"DRIVER_OPS\",\"NETDEV\",\"LOG_SINK\",\"AUTHZ\"],"
           "\"history_record_tag\":\"%s\","
           "\"vfs_include\":\"fl/vfs.h (separate)\"}\n",
           FL_CONTRACT_BUNDLE_REV, (int)FL_RESULT_OK, (int)FL_RESULT_ERR,
           FL_HISTORY_RECORD_TAG);
    return 0;
}

int cmd_contracts_run(int argc, char **argv) {
    const char *mode = "summary";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_contract_help();
            return 0;
        }
    }

    if (argc >= 2 && argv[1][0] != '-')
        mode = argv[1];

    if (!strcmp(mode, "json"))
        return print_json();
    if (!strcmp(mode, "summary"))
        return print_summary();

    fprintf(stderr, "contracts: unknown mode %s\n", mode);
    fprintf(stderr, "  try: contracts --help\n");
    return 1;
}
