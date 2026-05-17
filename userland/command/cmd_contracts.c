#include "cmd_decl.h"
#include "contract.h"
#include "contract_log_dispatch.h"
#include "fl/audit_log.h"
#include "fl/history_record.h"
#include <stdio.h>
#include <string.h>

static void print_contract_help(void) {
    printf(
        "Usage: contracts [summary|json] [--help]\n"
        "\n"
        "Print the public subsystem contract bundle (P0 headers under contracts/foundations/).\n"
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

static const char *const CONTRACT_SURFACE_NAMES[] = {
    "DRIVER_OPS", "NETDEV", "LOG_SINK", "AUTHZ", "FS_JAIL",
};

_Static_assert(sizeof(CONTRACT_SURFACE_NAMES) / sizeof(CONTRACT_SURFACE_NAMES[0]) ==
                   (size_t)FL_CONTRACT_SURFACE_COUNT,
               "CONTRACT_SURFACE_NAMES must match fl_contract_surface_t");

static int print_summary(void) {
    printf("contracts: bundle rev %d\n", FL_CONTRACT_BUNDLE_REV);
    printf("  fl_result_t: OK=%d ERR=%d INVAL=%d NOSYS=%d\n",
           (int)FL_RESULT_OK, (int)FL_RESULT_ERR, (int)FL_RESULT_INVAL, (int)FL_RESULT_NOSYS);
    printf("  surfaces (%zu):\n",
           sizeof(CONTRACT_SURFACE_NAMES) / sizeof(CONTRACT_SURFACE_NAMES[0]));
    for (unsigned i = 0; i < (unsigned)FL_CONTRACT_SURFACE_COUNT; i++)
        printf("    %u %s\n", i, CONTRACT_SURFACE_NAMES[i]);
    printf("  audit: separate from history; env %s file %s (see `audit` command)\n",
           FL_AUDIT_ENV, FL_AUDIT_REL_DEFAULT);
    printf("  jail: fl/jail_contract.h + fs_jail_* when the VM host sandbox is active\n");
    printf("  shell authz: **FL_PRINCIPAL=guest** denies destructive builtins and\n"
           "    foreign **execvp** before dispatch; optional hook via fl_shell_authz_set_hook\n");
    printf("  driver ops: probe/attach return fl_result_t; FL_RESULT_PROBE_SKIP when declining\n");
    printf("  log dispatch: fl_log_sink_emit_line (asm_mem_* buffer) + %d msgs/sec rate limit\n",
           FL_LOG_RL_MAX_PER_SEC);

    fl_authz_check_fn check = demo_authz_always_allow;
    printf("  demo authz(0,NULL) => %d\n", (int)check(0, NULL));
    return 0;
}

static int print_json(void) {
    printf("{\"bundle_rev\":%d,\"fl_result_ok\":%d,\"fl_result_err\":%d,"
           "\"fl_result_json_rc_min\":%d,\"fl_result_json_rc_max\":%d,"
           "\"log_rl_max_per_sec\":%d,"
           "\"surfaces\":[\"DRIVER_OPS\",\"NETDEV\",\"LOG_SINK\",\"AUTHZ\",\"FS_JAIL\"],"
           "\"history_record_tag\":\"%s\","
           "\"audit_env\":\"%s\",\"audit_log_relative\":\"%s\","
           "\"vfs_include\":\"fl/vfs.h (separate)\"}\n",
           FL_CONTRACT_BUNDLE_REV, (int)FL_RESULT_OK, (int)FL_RESULT_ERR,
           FL_RESULT_JSON_RC_MIN, FL_RESULT_JSON_RC_MAX, FL_LOG_RL_MAX_PER_SEC,
           FL_HISTORY_RECORD_TAG, FL_AUDIT_ENV, FL_AUDIT_REL_DEFAULT);
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
