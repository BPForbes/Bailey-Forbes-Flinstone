/**
 * Property tests: path normalization, directory entry validity, cluster invariants.
 */
#include "util.h"
#include "common.h"
#include "fl/history_record.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_path_dot(void) {
    strncpy(g_cwd, "/tmp/foo", sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    char out[256];
    resolve_path(".", out, sizeof(out));
    ASSERT(strcmp(out, "/tmp/foo") == 0);
    resolve_path("./bar", out, sizeof(out));
    ASSERT(strcmp(out, "/tmp/foo/bar") == 0);
    return 0;
}

static int test_path_dotdot(void) {
    strncpy(g_cwd, "/tmp/foo/bar", sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    char out[256];
    resolve_path("..", out, sizeof(out));
    ASSERT(strcmp(out, "/tmp/foo") == 0);
    resolve_path("../baz", out, sizeof(out));
    ASSERT(strcmp(out, "/tmp/foo/baz") == 0);
    resolve_path("a/b/../c", out, sizeof(out));
    ASSERT(strcmp(out, "/tmp/foo/bar/a/c") == 0);
    return 0;
}

static int test_path_absolute(void) {
    strncpy(g_cwd, "/tmp/foo", sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    char out[256];
    resolve_path("/usr/bin", out, sizeof(out));
    ASSERT(strcmp(out, "/usr/bin") == 0);
    resolve_path("/a/b/../c", out, sizeof(out));
    ASSERT(strcmp(out, "/a/c") == 0);
    return 0;
}

static int test_cluster_invariants(void) {
    ASSERT(g_total_clusters >= 0);
    ASSERT(g_cluster_size > 0 && g_cluster_size <= 65536);
    return 0;
}

static int test_history_contract_roundtrip(void) {
    char packed[512];
    char cmd_out[512];
    const char *cmd_in = "cd /tmp";
    size_t n = fl_history_record_pack(packed, sizeof packed, 9u,
                                      FL_CONTRACT_SURFACE_AUTHZ, FL_RESULT_INVAL,
                                      cmd_in);
    ASSERT(n != (size_t)-1);
    packed[n] = '\0';

    uint8_t br = 0;
    fl_contract_surface_t sf = FL_CONTRACT_SURFACE_DRIVER_OPS;
    fl_result_t rc = FL_RESULT_OK;
    ASSERT(fl_history_record_unpack_cmd(packed, cmd_out, sizeof cmd_out, &br,
                                          &sf, &rc) == 1);
    ASSERT(strcmp(cmd_out, cmd_in) == 0);
    ASSERT(br == 9u);
    ASSERT(sf == FL_CONTRACT_SURFACE_AUTHZ);
    ASSERT(rc == FL_RESULT_INVAL);

    ASSERT(fl_history_record_unpack_cmd("plain-cmd", cmd_out, sizeof cmd_out,
                                        NULL, NULL, NULL) == 0);
    ASSERT(strcmp(cmd_out, "plain-cmd") == 0);
    return 0;
}

int main(void) {
    printf("test_path_dot... ");
    if (test_path_dot() != 0) return 1;
    printf("OK\n");

    printf("test_path_dotdot... ");
    if (test_path_dotdot() != 0) return 1;
    printf("OK\n");

    printf("test_path_absolute... ");
    if (test_path_absolute() != 0) return 1;
    printf("OK\n");

    printf("test_cluster_invariants... ");
    if (test_cluster_invariants() != 0) return 1;
    printf("OK\n");

    printf("test_history_contract_roundtrip... ");
    if (test_history_contract_roundtrip() != 0) return 1;
    printf("OK\n");

    printf("All invariant tests passed.\n");
    return 0;
}
