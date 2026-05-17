/**
 * Property tests: path normalization, directory entry validity, cluster invariants.
 */
#include "util.h"
#include "common.h"
#include "fl/history_record.h"
#include "contract_result.h"
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

/* --- Additional fl_history_record_pack/unpack edge case tests --- */

static int test_history_pack_null_args(void) {
    char buf[64];
    /* NULL out buffer */
    ASSERT(fl_history_record_pack(NULL, sizeof buf, 1u,
                                  FL_CONTRACT_SURFACE_DRIVER_OPS,
                                  FL_RESULT_OK, "cmd") == (size_t)-1);
    /* NULL cmd */
    ASSERT(fl_history_record_pack(buf, sizeof buf, 1u,
                                  FL_CONTRACT_SURFACE_DRIVER_OPS,
                                  FL_RESULT_OK, NULL) == (size_t)-1);
    /* out_cap too small (< 16) */
    ASSERT(fl_history_record_pack(buf, 15u, 1u,
                                  FL_CONTRACT_SURFACE_DRIVER_OPS,
                                  FL_RESULT_OK, "cmd") == (size_t)-1);
    /* out_cap == 0 */
    ASSERT(fl_history_record_pack(buf, 0u, 1u,
                                  FL_CONTRACT_SURFACE_DRIVER_OPS,
                                  FL_RESULT_OK, "cmd") == (size_t)-1);
    return 0;
}

static int test_history_pack_reject_rs(void) {
    char buf[256];
    /* cmd containing RS (0x1E separator) must be rejected */
    char cmd_with_rs[8] = {'a', 0x1e, 'b', '\0'};
    ASSERT(fl_history_record_pack(buf, sizeof buf, 1u,
                                  FL_CONTRACT_SURFACE_DRIVER_OPS,
                                  FL_RESULT_OK, cmd_with_rs) == (size_t)-1);
    return 0;
}

static int test_history_pack_reject_newline(void) {
    char buf[256];
    /* cmd containing newline must be rejected */
    ASSERT(fl_history_record_pack(buf, sizeof buf, 1u,
                                  FL_CONTRACT_SURFACE_DRIVER_OPS,
                                  FL_RESULT_OK, "cmd\nwith\nnewline") == (size_t)-1);
    return 0;
}

static int test_history_unpack_null_args(void) {
    char out[64];
    /* NULL stored_line */
    ASSERT(fl_history_record_unpack_cmd(NULL, out, sizeof out,
                                        NULL, NULL, NULL) == 0);
    /* NULL cmd_out */
    ASSERT(fl_history_record_unpack_cmd("some-cmd", NULL, sizeof out,
                                        NULL, NULL, NULL) == 0);
    /* zero cap */
    ASSERT(fl_history_record_unpack_cmd("some-cmd", out, 0u,
                                        NULL, NULL, NULL) == 0);
    return 0;
}

static int test_history_unpack_plain_passthrough(void) {
    char out[16];
    /* Plain line shorter than cap copies verbatim */
    ASSERT(fl_history_record_unpack_cmd("ls", out, sizeof out,
                                        NULL, NULL, NULL) == 0);
    ASSERT(strcmp(out, "ls") == 0);

    /* Plain line longer than cap is truncated */
    const char *long_plain = "123456789012345678901234567890";
    char small[8];
    ASSERT(fl_history_record_unpack_cmd(long_plain, small, sizeof small,
                                        NULL, NULL, NULL) == 0);
    ASSERT(strlen(small) == 7u);
    ASSERT(small[7] == '\0');
    return 0;
}

static int test_history_unpack_malformed_no_sep2(void) {
    /* Starts with "FL1\x1e" but has no second RS → error, not legacy */
    char input[32];
    char out[64];
    memset(out, 'X', sizeof out);
    out[sizeof out - 1u] = '\0';
    int n = snprintf(input, sizeof input, "FL1\x1eJSON_NO_SEP2");
    ASSERT(n > 0);
    ASSERT(fl_history_record_unpack_cmd(input, out, sizeof out,
                                        NULL, NULL, NULL) == -1);
    ASSERT(out[0] == '\0');
    return 0;
}

static int test_history_unpack_all_surfaces(void) {
    char packed[512];
    char cmd_out[512];
    const fl_contract_surface_t surfaces[] = {
        FL_CONTRACT_SURFACE_DRIVER_OPS,
        FL_CONTRACT_SURFACE_NETDEV,
        FL_CONTRACT_SURFACE_LOG_SINK,
        FL_CONTRACT_SURFACE_AUTHZ,
        FL_CONTRACT_SURFACE_FS_JAIL,
    };
    const size_t nsurfaces = sizeof(surfaces) / sizeof(surfaces[0]);
    for (size_t i = 0; i < nsurfaces; i++) {
        size_t n = fl_history_record_pack(packed, sizeof packed, (uint8_t)i,
                                          surfaces[i], FL_RESULT_OK, "cmd");
        ASSERT(n != (size_t)-1);
        packed[n] = '\0';

        uint8_t br = 0xFF;
        fl_contract_surface_t sf = (fl_contract_surface_t)0xFF;
        fl_result_t rc = -99;
        ASSERT(fl_history_record_unpack_cmd(packed, cmd_out, sizeof cmd_out,
                                            &br, &sf, &rc) == 1);
        ASSERT(strcmp(cmd_out, "cmd") == 0);
        ASSERT(br == (uint8_t)i);
        ASSERT(sf == surfaces[i]);
        ASSERT(rc == FL_RESULT_OK);
    }
    return 0;
}

static int test_history_unpack_rc_json_out_of_bounds(void) {
    char input[160];
    char out[64];
    memset(out, 'X', sizeof out);
    out[sizeof out - 1u] = '\0';
    snprintf(input, sizeof input, "FL1%c{\"br\":1,\"sf\":0,\"rc\":200000}%cecho hi",
             FL_HISTORY_RECORD_SEP, FL_HISTORY_RECORD_SEP);
    ASSERT(fl_history_record_unpack_cmd(input, out, sizeof out, NULL, NULL, NULL) == -1);
    ASSERT(out[0] == '\0');
    snprintf(input, sizeof input, "FL1%c{\"br\":1,\"sf\":0,\"rc\":-200000}%cecho hi",
             FL_HISTORY_RECORD_SEP, FL_HISTORY_RECORD_SEP);
    ASSERT(fl_history_record_unpack_cmd(input, out, sizeof out, NULL, NULL, NULL) == -1);
    ASSERT(out[0] == '\0');
    /* Boundary: within FL_RESULT_JSON_RC_* must succeed */
    snprintf(input, sizeof input, "FL1%c{\"br\":1,\"sf\":0,\"rc\":%d}%cecho ok",
             FL_HISTORY_RECORD_SEP, FL_RESULT_JSON_RC_MAX, FL_HISTORY_RECORD_SEP);
    ASSERT(fl_history_record_unpack_cmd(input, out, sizeof out, NULL, NULL, NULL) == 1);
    ASSERT(strcmp(out, "echo ok") == 0);
    return 0;
}

static int test_batch_argv_contracts_audit(void) {
    char *av[] = {"prog", "contracts", "audit", "show", "5"};
    ASSERT(fl_batch_contracts_tokens_count(5, av, 1) == 1);
    ASSERT(fl_batch_audit_tokens_count(5, av, 2) == 3);
    ASSERT(fl_batch_contracts_tokens_count(5, av, 2) == 1);
    char *av2[] = {"prog", "contracts", "summary"};
    ASSERT(fl_batch_contracts_tokens_count(3, av2, 1) == 2);
    char *av3[] = {"prog", "audit", "ring"};
    ASSERT(fl_batch_audit_tokens_count(3, av3, 1) == 2);
    return 0;
}

static int test_history_unpack_negative_rc(void) {
    char packed[512];
    char cmd_out[512];
    const fl_result_t codes[] = {
        FL_RESULT_OK, FL_RESULT_ERR, FL_RESULT_INVAL, FL_RESULT_NOSYS,
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        size_t n = fl_history_record_pack(packed, sizeof packed, 1u,
                                          FL_CONTRACT_SURFACE_DRIVER_OPS,
                                          codes[i], "test");
        ASSERT(n != (size_t)-1);
        packed[n] = '\0';

        fl_result_t rc_out = 999;
        ASSERT(fl_history_record_unpack_cmd(packed, cmd_out, sizeof cmd_out,
                                            NULL, NULL, &rc_out) == 1);
        ASSERT(rc_out == codes[i]);
    }
    return 0;
}

static int test_history_unpack_strips_trailing_newline(void) {
    /* Manually construct a record with trailing \r\n to test strip logic */
    char packed[512];
    char cmd_out[512];
    const char *cmd_in = "echo hello";
    size_t n = fl_history_record_pack(packed, sizeof packed, 2u,
                                      FL_CONTRACT_SURFACE_NETDEV, FL_RESULT_OK,
                                      cmd_in);
    ASSERT(n != (size_t)-1);
    packed[n] = '\0';
    /* pack appends \n; unpack should strip it */
    ASSERT(fl_history_record_unpack_cmd(packed, cmd_out, sizeof cmd_out,
                                        NULL, NULL, NULL) == 1);
    ASSERT(strcmp(cmd_out, cmd_in) == 0);
    return 0;
}

static int test_contract_constants(void) {
    /* Verify the contract result codes match expected values (P0-2) */
    ASSERT(FL_RESULT_OK == 0);
    ASSERT(FL_RESULT_ERR == -1);
    ASSERT(FL_RESULT_INVAL == -22);
    ASSERT(FL_RESULT_NOSYS == -38);

    /* Bundle revision */
    ASSERT(FL_CONTRACT_BUNDLE_REV == 6);
    ASSERT(FL_CONTRACT_COMPILE_EXTENSIONS == 0);
    ASSERT(FL_RESULT_WIRE_MIN == FL_RESULT_JSON_RC_MIN);
    ASSERT(FL_RESULT_WIRE_MAX == FL_RESULT_JSON_RC_MAX);

    /* Surface enum ordering */
    ASSERT((int)FL_CONTRACT_SURFACE_DRIVER_OPS == 0);
    ASSERT((int)FL_CONTRACT_SURFACE_NETDEV == 1);
    ASSERT((int)FL_CONTRACT_SURFACE_LOG_SINK == 2);
    ASSERT((int)FL_CONTRACT_SURFACE_AUTHZ == 3);
    ASSERT((int)FL_CONTRACT_SURFACE_FS_JAIL == 4);
    ASSERT((int)FL_CONTRACT_SURFACE_COUNT == 5);

    /* Authz decisions */
    ASSERT((int)FL_AUTHZ_DENY == 0);
    ASSERT((int)FL_AUTHZ_ALLOW == 1);

    /* History record tag and separator */
    ASSERT(strcmp(FL_HISTORY_RECORD_TAG, "FL1") == 0);
    ASSERT((unsigned char)FL_HISTORY_RECORD_SEP == 0x1eu);
    return 0;
}

static int test_history_record_tag_in_packed(void) {
    char packed[256];
    size_t n = fl_history_record_pack(packed, sizeof packed, 0u,
                                      FL_CONTRACT_SURFACE_DRIVER_OPS,
                                      FL_RESULT_OK, "ls");
    ASSERT(n != (size_t)-1);
    /* Output must start with "FL1\x1e" */
    ASSERT(packed[0] == 'F');
    ASSERT(packed[1] == 'L');
    ASSERT(packed[2] == '1');
    ASSERT((unsigned char)packed[3] == 0x1eu);
    /* Must end with newline */
    ASSERT(packed[n - 1] == '\n');
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

    printf("test_history_pack_null_args... ");
    if (test_history_pack_null_args() != 0) return 1;
    printf("OK\n");

    printf("test_history_pack_reject_rs... ");
    if (test_history_pack_reject_rs() != 0) return 1;
    printf("OK\n");

    printf("test_history_pack_reject_newline... ");
    if (test_history_pack_reject_newline() != 0) return 1;
    printf("OK\n");

    printf("test_history_unpack_null_args... ");
    if (test_history_unpack_null_args() != 0) return 1;
    printf("OK\n");

    printf("test_history_unpack_plain_passthrough... ");
    if (test_history_unpack_plain_passthrough() != 0) return 1;
    printf("OK\n");

    printf("test_history_unpack_malformed_no_sep2... ");
    if (test_history_unpack_malformed_no_sep2() != 0) return 1;
    printf("OK\n");

    printf("test_history_unpack_all_surfaces... ");
    if (test_history_unpack_all_surfaces() != 0) return 1;
    printf("OK\n");

    printf("test_history_unpack_negative_rc... ");
    if (test_history_unpack_negative_rc() != 0) return 1;
    printf("OK\n");

    printf("test_history_unpack_rc_json_out_of_bounds... ");
    if (test_history_unpack_rc_json_out_of_bounds() != 0) return 1;
    printf("OK\n");

    printf("test_batch_argv_contracts_audit... ");
    if (test_batch_argv_contracts_audit() != 0) return 1;
    printf("OK\n");

    printf("test_history_unpack_strips_trailing_newline... ");
    if (test_history_unpack_strips_trailing_newline() != 0) return 1;
    printf("OK\n");

    printf("test_contract_constants... ");
    if (test_contract_constants() != 0) return 1;
    printf("OK\n");

    printf("test_history_record_tag_in_packed... ");
    if (test_history_record_tag_in_packed() != 0) return 1;
    printf("OK\n");

    printf("All invariant tests passed.\n");
    return 0;
}
