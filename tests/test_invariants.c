/**
 * Property tests: path normalization, directory entry validity, cluster invariants.
 */
#include "util.h"
#include "common.h"
#include "fl/history_record.h"
#include "contract_result.h"
#include "contract_runtime.h"
#include "contract_identity.h"
#include "contract_drivers.h"
#include "contract_storage.h"
#include "contract_observability.h"
#include "contract_operations.h"
#include "contract_virtualization.h"
#include "contract_hardening.h"
#include "contract_networking.h"
#include "fl/authz_subsystem.h"
#include "fl/session.h"
#include "contract_p7_shell_batch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

_Static_assert(FL_CONTRACT_P8_VIRTUALIZATION_REV == 1, "tests track P8 umbrella rev");
_Static_assert(FL_CONTRACT_P8_1_TIMING_CONTRACT_DEFINED == 1, "P8-1 shard must stay defined");
_Static_assert(FL_CONTRACT_P8_2_VIRTIO_GUEST_CONTRACT_DEFINED == 1, "P8-2 shard must stay defined");
_Static_assert(FL_CONTRACT_P8_3_QEMU_LAB_CONTRACT_DEFINED == 1, "P8-3 shard must stay defined");

_Static_assert(FL_CONTRACT_P9_HARDENING_REV == 2, "tests track P9 umbrella rev");
_Static_assert(FL_CONTRACT_P9_1_FUZZ_CONTRACT_DEFINED == 1, "P9-1 shard must stay defined");
_Static_assert(FL_CONTRACT_P9_2_STATIC_ANALYSIS_CONTRACT_DEFINED == 1, "P9-2 shard must stay defined");
_Static_assert(FL_CONTRACT_P9_3_SMP_CONTRACT_DEFINED == 1, "P9-3 shard must stay defined");
_Static_assert(FL_CONTRACT_P9_4_RCU_CONTRACT_DEFINED == 1, "P9-4 shard must stay defined");

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

/* Issue #219 / #204: batch driver clamps token spans to argc-i (sh.c pattern). */
static int batch_argv_clamp_tokens(int tokens_count, int argc, int i) {
    if (tokens_count > argc - i)
        tokens_count = argc - i;
    return tokens_count;
}

static int test_batch_argv_sh_clamp_edges(void) {
    ASSERT(batch_argv_clamp_tokens(99, 2, 1) == 1);
    ASSERT(batch_argv_clamp_tokens(4, 5, 1) == 4);
    ASSERT(batch_argv_clamp_tokens(0, 5, 1) == 0);
    ASSERT(FL_CONTRACT_P7_BATCH_MAX_TOKENS_TOTAL <= 16u);
    return 0;
}

/* Issue #219 / #186: session mutex-backed state, elevation, and sudo scope. */
static int test_session_hardening_issue186(void) {
    char tmpl[] = "/tmp/fl_inv_sessXXXXXX";
    char dbpath[sizeof(tmpl) + 8];
    int fd;

    fd = mkstemp(tmpl);
    ASSERT(fd >= 0);
    close(fd);
    snprintf(dbpath, sizeof(dbpath), "%s.db", tmpl);
    ASSERT(rename(tmpl, dbpath) == 0);

    (void)setenv("FL_USERS_DB_PATH", dbpath, 1);
    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
    fl_session_init();

    ASSERT(fl_session_set_user(NULL) == FL_RESULT_INVAL);
    ASSERT(fl_session_set_user("") == FL_RESULT_INVAL);
    ASSERT(fl_session_set_user("no_such_user_xyz") == FL_RESULT_NOENT);

    ASSERT(fl_session_login("root", "root") == FL_RESULT_OK);
    ASSERT(fl_session_is_elevated_account() == 1);
    ASSERT(fl_session_has_elevation() == 1);
    ASSERT(fl_session_jail_privileged() == 1);

    ASSERT(fl_session_login("flinstone", "flinstone") == FL_RESULT_OK);
    ASSERT(strcmp(fl_session_current_user(), "flinstone") == 0);
    ASSERT(fl_session_is_elevated_account() == 0);
    ASSERT(fl_session_has_elevation() == 0);
    ASSERT(fl_session_verify_password("flinstone") == 1);
    ASSERT(fl_session_verify_password("wrong") == 0);
    ASSERT(fl_session_jail_privileged() == 0);

    {
        fl_elevation_token_t tok = FL_ELEVATION_TOKEN_NONE;
        ASSERT(fl_session_grant_elevation("issue186", &tok) == FL_RESULT_OK);
        ASSERT(fl_session_has_elevation() == 1);
        ASSERT(fl_elevation_active(tok) == 1);
        fl_session_drop_elevation();
        ASSERT(fl_session_has_elevation() == 0);
        ASSERT(fl_elevation_active(tok) == 0);
    }

    fl_session_begin_sudo_scope();
    ASSERT(fl_session_in_sudo_scope() == 1);
    ASSERT(fl_session_jail_privileged() == 1);
    fl_session_end_sudo_scope();
    ASSERT(fl_session_in_sudo_scope() == 0);
    ASSERT(fl_session_jail_privileged() == 0);

    ASSERT(fl_session_logout() == FL_RESULT_OK);
    unlink(dbpath);
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
    ASSERT(FL_RESULT_NOMEM == -12);
    ASSERT(FL_RESULT_ACCES == -13);
    ASSERT(FL_RESULT_NOENT == -2);
    ASSERT(FL_RESULT_BUSY == -16);
    ASSERT(FL_RESULT_TIMEDOUT == -110);

    ASSERT(FL_CONTRACT_P0_FOUNDATIONS_REV == 4);
    ASSERT(FL_CONTRACT_P0_3_CI_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P0_CI_INTERCHANGE_REV == 2);
    ASSERT((int)fl_contract_p0_ci_surface_default_gate ==
           FL_CONTRACT_P0_CI_SURFACE_DEFAULT_GATE);
    ASSERT((int)fl_contract_p0_ci_surface_network_interop ==
           FL_CONTRACT_P0_CI_SURFACE_NETWORK_INTEROP);
    ASSERT(FL_CONTRACT_P0_CI_SURFACE_COUNT == 2);
    ASSERT(strcmp(FL_CONTRACT_P0_CI_SKIP_TAP_ENV_NAME, "SKIP_TAP") == 0);
    ASSERT(FL_RESULT_MIN == FL_RESULT_JSON_RC_MIN);
    ASSERT(FL_RESULT_MAX == FL_RESULT_JSON_RC_MAX);
    ASSERT(FL_CONTRACT_P0_4_GIC_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P0_GIC_INTERCHANGE_REV == 1);
    ASSERT((int)fl_contract_p0_gic_surface_eoi_path ==
           FL_CONTRACT_P0_GIC_SURFACE_EOI_PATH);
    ASSERT(FL_CONTRACT_P0_5_IDT_IRQ0_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P0_IDT_INTERCHANGE_REV == 1);
    ASSERT(FL_CONTRACT_P0_6_GDT_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P0_GDT_INTERCHANGE_REV == 1);
    ASSERT(FL_CONTRACT_P0_7_FDT_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P0_FDT_INTERCHANGE_REV == 1);
    ASSERT(FL_CONTRACT_P0_FDT_WAIVER_ACCEPTED == 0);
    ASSERT(FL_CONTRACT_P0_8_UART_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P0_UART_INTERCHANGE_REV == 1);
    ASSERT(FL_CONTRACT_P0_FS_JAIL_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P0_FS_JAIL_INTERCHANGE_REV == 1);
    ASSERT(strcmp(FL_CONTRACT_P0_FS_JAIL_HOST_SUBDIR, "vm_hostfs") == 0);
    ASSERT(FL_CONTRACT_P0_UART_WAIVER_ACCEPTED == 0);
    ASSERT(FL_CONTRACT_SURFACE_LOG_SINK_LIFETIME_CALLER_STACK == 0);
    ASSERT(FL_CONTRACT_SURFACE_LOG_SINK_CONCURRENCY ==
           FL_CONTRACT_P0_SURFACE_CONCURRENCY_LOCK_GUARDED);
    ASSERT(FL_CONTRACT_COMPILE_EXTENSIONS == 0);

    ASSERT(FL_CONTRACT_P1_RUNTIME_REV == 2);
    ASSERT(FL_CONTRACT_P1_VOCABULARY_LOCK == 1);
    ASSERT(FL_CONTRACT_P1_1_EXECUTION_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_2_ADDRESS_SPACE_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_3_PREEMPTION_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_4_PMM_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_5_ARENAS_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_6_DRIVER_REENTRANCY_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_7_TIMEKEEPING_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_WORKQUEUE_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_9_RECLAIM_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P1_10_WATCHDOG_CONTRACT_DEFINED == 1);
    ASSERT(FL_WQ_LAYER_URGENT == 0);
    ASSERT(FL_WQ_PENDING_MAX == 64u);

    ASSERT(FL_CONTRACT_P2_IDENTITY_REV == 3);
    ASSERT(strcmp(FL_CREDENTIAL_STORE_LAB_SQLITE_REL, "userland/shell/fl_users.db") == 0);
    ASSERT(FL_CONTRACT_P2_VOCABULARY_LOCK == 1);
    ASSERT(FL_CONTRACT_P2_1_PRINCIPAL_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P2_2_CREDENTIAL_STORE_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P2_3_AUTHZ_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P2_4_ELEVATION_CONTRACT_DEFINED == 1);

    ASSERT(strcmp(FL_PRINCIPAL_ENV_NAME, "FL_PRINCIPAL") == 0);
    ASSERT(strcmp(FL_PRINCIPAL_GUEST_LITERAL, "guest") == 0);
    ASSERT(FL_PRINCIPAL_ID_GUEST == 2u);
    ASSERT((unsigned)FL_AUTHZ_OP_SHELL_FOREIGN_EXEC == 100u);
    ASSERT(FL_ELEVATION_TOKEN_NONE == 0u);
    ASSERT(FL_CREDENTIAL_USERNAME_MAX_CHARS == 63u);

    ASSERT(FL_RESULT_WIRE_MIN == FL_RESULT_JSON_RC_MIN);
    ASSERT(FL_RESULT_WIRE_MAX == FL_RESULT_JSON_RC_MAX);

    ASSERT(FL_CONTRACT_P4_DRIVERS_REV == 4);
    ASSERT(FL_CONTRACT_P4_2_HARDIRQ_NO_SLEEP == 1);
    ASSERT(FL_CONTRACT_P4_3_LAB_IOMMU_BYPASS_ASSUMED == 1);
    ASSERT(FL_CONTRACT_P4_4_DESC_PUBLISH_BARRIER_REQUIRED == 1);
    ASSERT(FL_CONTRACT_P4_8_KWORKER_CONTRACT_DEFINED == 1);

    ASSERT(FL_CONTRACT_P5_STORAGE_REV == 3);
    ASSERT(FL_CONTRACT_P5_VOCABULARY_LOCK == 1);
    ASSERT(FL_CONTRACT_P5_1_VFS_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P5_2_PLUGGABLE_FS_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P5_3_PAGE_CACHE_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P5_4_WRITEBACK_CONTRACT_DEFINED == 1);
    ASSERT(FL_VFS_SYMLOOP_MAX == 40);
    ASSERT(FL_VFS_OPEN_FD_MAX == 256u);
    ASSERT(FL_VFS_DIRENT_NAME_MAX_CHARS == 255u);
    ASSERT((int)FL_VFS_MOUNT_FLAG_RDONLY == 1);
    ASSERT(FL_PAGE_CACHE_BLOCK_SHIFT_MIN == 9u);
    ASSERT(FL_PAGE_CACHE_MAX_ENTRIES == 4096u);

    ASSERT(FL_CONTRACT_P6_OBSERVABILITY_REV == 2);
    ASSERT(FL_CONTRACT_P6_VOCABULARY_LOCK == 1);
    ASSERT(FL_CONTRACT_P6_1_STRUCTURED_LOG_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P6_LOG_MIN_LEVEL_DEFAULT == FL_LOG_INFO);
    ASSERT(FL_CONTRACT_P6_CORRELATION_ID_INVALID == 0u);
    ASSERT(FL_CONTRACT_P6_4_AUDIT_TRAIL_CONTRACT_DEFINED == 1);
    ASSERT((int)FL_CONTRACT_P6_TRACE_COUNT == 4);

    ASSERT(FL_CONTRACT_P7_OPERATIONS_REV == 3);
    ASSERT(FL_CONTRACT_P7_VOCABULARY_LOCK == 1);
    ASSERT(FL_CONTRACT_P7_1_SERVICE_SUPERVISION_CONTRACT_DEFINED == 1);
    ASSERT(FL_CONTRACT_P7_SUPERVISED_SERVICE_MAX == 32u);
    ASSERT((int)FL_CONTRACT_P7_SERVICE_STATE_FAILED == 4);
    ASSERT(FL_CONTRACT_P7_RESTART_MAX_ATTEMPTS_DEFAULT == 5u);
    ASSERT(FL_CONTRACT_P7_VERSION_COMPONENT_MAX == 65535u);
    ASSERT(FL_CONTRACT_P7_BATCH_AUDIT_SHOW_MAX_TOKENS == 3u);
    ASSERT(FL_CONTRACT_P7_BATCH_MAX_TOKENS_TOTAL == 16u);

    ASSERT(FL_CONTRACT_P3_NETWORKING_REV == 7);
    ASSERT(FL_CONTRACT_P3_PACKET_CONTRACT_DEFINED == 1);
    ASSERT(FL_NET_PKT_VALID_L2 == (unsigned)(1u << 0));
    ASSERT((int)FL_NET_PIPE_STAGE_DELIVER == 6);
    ASSERT(FL_CONTRACT_P3_14_BACKGROUND_CONTRACT_DEFINED == 1);
    ASSERT(FL_NET_BG_TAG_ARP_TICK == 0x0314u);

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

static int test_authz_subsystem_guest_denies(void) {
    char prev[256];
    const char *old = getenv(FL_PRINCIPAL_ENV_NAME);
    if (old) {
        strncpy(prev, old, sizeof prev - 1);
        prev[sizeof prev - 1] = '\0';
    }
    (void)setenv(FL_PRINCIPAL_ENV_NAME, FL_PRINCIPAL_GUEST_LITERAL, 1);
    ASSERT(fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_VFS_PRIVILEGED, NULL) ==
           FL_AUTHZ_DENY);
    ASSERT(fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_NETDEV_REGISTER, NULL) ==
           FL_AUTHZ_DENY);
    ASSERT(fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_MOUNT, NULL) == FL_AUTHZ_DENY);
    ASSERT(fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_UNSPECIFIED, NULL) ==
           FL_AUTHZ_ALLOW);
    if (old)
        (void)setenv(FL_PRINCIPAL_ENV_NAME, prev, 1);
    else
        (void)unsetenv(FL_PRINCIPAL_ENV_NAME);
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

    printf("test_batch_argv_sh_clamp_edges... ");
    if (test_batch_argv_sh_clamp_edges() != 0) return 1;
    printf("OK\n");

    printf("test_session_hardening_issue186... ");
    if (test_session_hardening_issue186() != 0) return 1;
    printf("OK\n");

    printf("test_history_unpack_strips_trailing_newline... ");
    if (test_history_unpack_strips_trailing_newline() != 0) return 1;
    printf("OK\n");

    printf("test_contract_constants... ");
    if (test_contract_constants() != 0) return 1;
    printf("OK\n");

    printf("test_authz_subsystem_guest_denies... ");
    if (test_authz_subsystem_guest_denies() != 0) return 1;
    printf("OK\n");

    printf("test_history_record_tag_in_packed... ");
    if (test_history_record_tag_in_packed() != 0) return 1;
    printf("OK\n");

    printf("All invariant tests passed.\n");
    return 0;
}
