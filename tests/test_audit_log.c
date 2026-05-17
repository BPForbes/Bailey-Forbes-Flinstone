/**
 * test_audit_log.c — Unit tests for the audit log subsystem (userland/shell/audit_log.c)
 * and the fl_log_sink_t / fl_authz contract types introduced in this PR.
 *
 * Tests cover:
 *   fl_audit_show_last_lines(): missing file, empty file, n-clamping,
 *       writing known lines and reading back last N
 *   fl_audit_shell_completed(): no-op when FL_AUDIT unset, no-op when cmd==NULL,
 *       writes record when FL_AUDIT=1
 *   fl_audit_set_sink(): sink emit callback is invoked on audit event
 *   fl_log_sink_t / fl_log_level_t values
 *   fl_authz_decision_t values and fl_authz_check_fn callable
 */

#include "fl/audit_log.h"
#include "fl/contract.h"
#include "fl/contract_log_dispatch.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* POSIX temp dir helpers */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while(0)

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static char g_tmpdir[PATH_MAX];

static int make_tmpdir(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !base[0])
        base = "/tmp";
    snprintf(g_tmpdir, sizeof g_tmpdir, "%s/fl_audit_test_XXXXXX", base);
    if (!mkdtemp(g_tmpdir)) {
        perror("mkdtemp");
        return -1;
    }
    return 0;
}

static void rmrf_dir(const char *path) {
    /* Simple: remove the audit log file and then rmdir */
    char audit_path[PATH_MAX];
    snprintf(audit_path, sizeof audit_path, "%s/%s", path, FL_AUDIT_REL_DEFAULT);
    unlink(audit_path);
    rmdir(path);
}

/* Helper: change to tmpdir and back */
static char g_saved_cwd[PATH_MAX];

static int enter_tmpdir(void) {
    if (!getcwd(g_saved_cwd, sizeof g_saved_cwd)) {
        perror("getcwd");
        return -1;
    }
    if (chdir(g_tmpdir) != 0) {
        perror("chdir tmpdir");
        return -1;
    }
    return 0;
}

static void leave_tmpdir(void) {
    (void)chdir(g_saved_cwd);
}

/* Write lines to the audit log file directly (bypassing env check) */
static int write_audit_lines(const char **lines, int count) {
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "w");
    if (!fp)
        return -1;
    for (int i = 0; i < count; i++) {
        if (fprintf(fp, "%s\n", lines[i]) < 0) {
            fclose(fp);
            return -1;
        }
    }
    return fclose(fp) == 0 ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * Tests: fl_audit_show_last_lines()
 * ---------------------------------------------------------------------- */

static int test_audit_show_no_file(void) {
    ASSERT(enter_tmpdir() == 0);
    /* Ensure the audit log does not exist */
    unlink(FL_AUDIT_REL_DEFAULT);

    /* Should return 0 (not an error) and print a "not found" message */
    int rc = fl_audit_show_last_lines(5);
    ASSERT(rc == 0);

    leave_tmpdir();
    return 0;
}

static int test_audit_show_empty_file(void) {
    ASSERT(enter_tmpdir() == 0);

    /* Create an empty audit log */
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "w");
    ASSERT(fp != NULL);
    fclose(fp);

    /* Should return 0 and print "(audit log empty)" */
    int rc = fl_audit_show_last_lines(10);
    ASSERT(rc == 0);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_show_n_clamping(void) {
    ASSERT(enter_tmpdir() == 0);

    /* Write a few lines so the file is non-empty */
    const char *lines[] = { "line1", "line2" };
    ASSERT(write_audit_lines(lines, 2) == 0);

    /* n <= 0 should be clamped to 32 (no crash) */
    ASSERT(fl_audit_show_last_lines(0) == 0);
    ASSERT(fl_audit_show_last_lines(-1) == 0);

    /* n > 10000 should be clamped to 32 (no crash) */
    ASSERT(fl_audit_show_last_lines(10001) == 0);
    ASSERT(fl_audit_show_last_lines(99999) == 0);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_show_returns_last_n(void) {
    ASSERT(enter_tmpdir() == 0);

    /* Write 10 distinct lines */
    const char *lines[] = {
        "alpha", "bravo", "charlie", "delta", "echo",
        "foxtrot", "golf", "hotel", "india", "juliet",
    };
    int total = (int)(sizeof lines / sizeof lines[0]);
    ASSERT(write_audit_lines(lines, total) == 0);

    /* Ask for last 3 — should succeed without error */
    ASSERT(fl_audit_show_last_lines(3) == 0);

    /* Ask for more lines than exist — should succeed */
    ASSERT(fl_audit_show_last_lines(100) == 0);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_audit_shell_completed()
 * ---------------------------------------------------------------------- */

static int test_audit_completed_noop_when_env_unset(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    /* Ensure FL_AUDIT is unset */
    unsetenv(FL_AUDIT_ENV);
    fl_audit_shell_completed("echo hello", 0);

    /* File must NOT have been created */
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp == NULL);

    leave_tmpdir();
    return 0;
}

static int test_audit_completed_noop_env_zero(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "0", 1);
    fl_audit_shell_completed("echo hello", 0);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp == NULL);

    unsetenv(FL_AUDIT_ENV);
    leave_tmpdir();
    return 0;
}

static int test_audit_completed_noop_null_cmd(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    /* NULL cmd must not crash */
    fl_audit_shell_completed(NULL, 0);

    /* File must NOT have been created */
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp == NULL);

    unsetenv(FL_AUDIT_ENV);
    leave_tmpdir();
    return 0;
}

static int test_audit_completed_writes_file(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_shell_completed("ls /tmp", 0);
    unsetenv(FL_AUDIT_ENV);

    /* File must have been created and be non-empty */
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    /* Record must contain expected fields */
    ASSERT(strstr(buf, "type=shell") != NULL);
    ASSERT(strstr(buf, "host_rc=0") != NULL);
    ASSERT(strstr(buf, "fl_rc=0") != NULL);
    ASSERT(strstr(buf, "cmd=ls /tmp") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_completed_nonzero_exit_maps_to_err(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_shell_completed("badcmd", 1);
    unsetenv(FL_AUDIT_ENV);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    /* Non-zero host_rc → fl_rc maps to FL_RESULT_ERR (-1) */
    ASSERT(strstr(buf, "host_rc=1") != NULL);
    ASSERT(strstr(buf, "fl_rc=-1") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_completed_sanitizes_control_chars(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    /* cmd with control chars and quotes that should be sanitized */
    fl_audit_shell_completed("cmd\x01with\"quotes\\slash", 0);
    unsetenv(FL_AUDIT_ENV);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    /* The cmd= portion must not contain raw control chars, backslash, or double-quote */
    const char *cmd_field = strstr(buf, "cmd=");
    ASSERT(cmd_field != NULL);
    for (size_t i = 4; cmd_field[i] && cmd_field[i] != '\n'; i++) {
        unsigned char c = (unsigned char)cmd_field[i];
        ASSERT(c >= 32u);        /* no control chars */
        ASSERT(c != (unsigned char)'"');
        ASSERT(c != (unsigned char)'\\');
    }

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_audit_set_sink() / fl_log_sink_t
 * ---------------------------------------------------------------------- */

static int g_emit_called = 0;
static int g_emit_level = -1;
static char g_emit_msg[1024];

static void test_emit_fn(struct fl_log_sink *sink, int level,
                         int facility, const char *msg) {
    (void)sink;
    (void)facility;
    g_emit_called++;
    g_emit_level = level;
    if (msg)
        strncpy(g_emit_msg, msg, sizeof g_emit_msg - 1u);
}

static int test_audit_sink_invoked(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    static const fl_log_sink_ops_t ops = { .emit = test_emit_fn };
    fl_log_sink_t sink = { .ops = &ops, .impl = NULL };
    fl_audit_set_sink(&sink);

    g_emit_called = 0;
    g_emit_level = -1;
    g_emit_msg[0] = '\0';

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_shell_completed("ping test", 0);
    unsetenv(FL_AUDIT_ENV);

    /* Reset sink to avoid dangling pointer after this test */
    fl_audit_set_sink(NULL);

    ASSERT(g_emit_called >= 1);
    ASSERT(g_emit_level == (int)FL_LOG_INFO);
    ASSERT(strstr(g_emit_msg, "type=shell") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_sink_null_safe(void) {
    /* Setting sink to NULL and completing a command must not crash */
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    fl_audit_set_sink(NULL);
    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_shell_completed("echo ok", 0);
    unsetenv(FL_AUDIT_ENV);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_log_level_t values
 * ---------------------------------------------------------------------- */

static int test_log_level_values(void) {
    ASSERT((int)FL_LOG_EMERG   == 0);
    ASSERT((int)FL_LOG_ALERT   == 1);
    ASSERT((int)FL_LOG_CRIT    == 2);
    ASSERT((int)FL_LOG_ERR     == 3);
    ASSERT((int)FL_LOG_WARNING == 4);
    ASSERT((int)FL_LOG_NOTICE  == 5);
    ASSERT((int)FL_LOG_INFO    == 6);
    ASSERT((int)FL_LOG_DEBUG   == 7);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_authz_decision_t and fl_authz_check_fn
 * ---------------------------------------------------------------------- */

static fl_authz_decision_t always_allow_fn(unsigned op, void *ctx) {
    (void)op; (void)ctx;
    return FL_AUTHZ_ALLOW;
}

static fl_authz_decision_t always_deny_fn(unsigned op, void *ctx) {
    (void)op; (void)ctx;
    return FL_AUTHZ_DENY;
}

static int test_authz_decisions(void) {
    ASSERT((int)FL_AUTHZ_DENY  == 0);
    ASSERT((int)FL_AUTHZ_ALLOW == 1);

    fl_authz_check_fn allow_fn = always_allow_fn;
    fl_authz_check_fn deny_fn  = always_deny_fn;

    ASSERT(allow_fn(0u, NULL) == FL_AUTHZ_ALLOW);
    ASSERT(deny_fn(0u,  NULL) == FL_AUTHZ_DENY);
    ASSERT(allow_fn(99u, (void *)0x1) == FL_AUTHZ_ALLOW);
    ASSERT(deny_fn(99u,  (void *)0x1) == FL_AUTHZ_DENY);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_log_sink_t struct layout (compile-time sanity)
 * ---------------------------------------------------------------------- */

static int test_log_sink_ops_struct(void) {
    /* Verify fl_log_sink_t can be constructed and ops pointer used safely */
    static const fl_log_sink_ops_t ops = { .emit = test_emit_fn };
    fl_log_sink_t sink = { .ops = &ops, .impl = (void *)0xDEAD };

    ASSERT(sink.ops == &ops);
    ASSERT(sink.ops->emit == test_emit_fn);
    ASSERT(sink.impl == (void *)0xDEAD);

    /* Calling emit via the ops vtable must work */
    g_emit_called = 0;
    sink.ops->emit(&sink, (int)FL_LOG_WARNING, 1, "test-msg");
    ASSERT(g_emit_called == 1);
    ASSERT(g_emit_level == (int)FL_LOG_WARNING);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_log_sink_emit_line dispatch + rate limit (ASM-backed buffer)
 * ---------------------------------------------------------------------- */

static int test_log_dispatch_rate_limit(void) {
    static const fl_log_sink_ops_t ops = { .emit = test_emit_fn };
    fl_log_sink_t sink = { .ops = &ops, .impl = NULL };

    fl_log_rate_limit_reset_for_tests();
    for (int i = 0; i < FL_LOG_RL_MAX_PER_SEC; i++) {
        g_emit_called = 0;
        ASSERT(fl_log_sink_emit_line(&sink, (int)FL_LOG_INFO, FL_LOG_FACILITY_AUDIT, "x") ==
               FL_RESULT_OK);
        ASSERT(g_emit_called == 1);
    }

    /*
     * The (FL_LOG_RL_MAX_PER_SEC + 1)th fl_log_sink_emit_line may land in a new monotonic
     * second; poll fl_log_sink_emit_line in a tight bounded loop (referencing
     * FL_RESULT_OK / FL_RESULT_ERR / g_emit_called / &sink) until FL_RESULT_ERR (expected
     * rate limit) — at most two full per-second windows need ~2 * FL_LOG_RL_MAX_PER_SEC + 1
     * OK emits before the next line must be rejected.
     */
    int saw_rate_limit = 0;
    const int max_tight = 2 * FL_LOG_RL_MAX_PER_SEC + 32;
    for (int k = 0; k < max_tight; k++) {
        g_emit_called = 0;
        fl_result_t rc =
            fl_log_sink_emit_line(&sink, (int)FL_LOG_INFO, FL_LOG_FACILITY_AUDIT, "overflow");
        if (rc == FL_RESULT_ERR) {
            ASSERT(g_emit_called == 0);
            saw_rate_limit = 1;
            break;
        }
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "FAIL [%s:%d]: unexpected fl_log_sink_emit_line return %d\n",
                    __FILE__, __LINE__, (int)rc);
            return 1;
        }
        ASSERT(g_emit_called == 1);
    }
    ASSERT(saw_rate_limit);
    return 0;
}

static int test_log_dispatch_null_sink(void) {
    ASSERT(fl_log_sink_emit_line(NULL, (int)FL_LOG_INFO, FL_LOG_FACILITY_AUDIT, "nope") ==
           FL_RESULT_INVAL);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: audit constants (FL_AUDIT_REL_DEFAULT, FL_AUDIT_ENV)
 * ---------------------------------------------------------------------- */

static int test_audit_constants(void) {
    ASSERT(strcmp(FL_AUDIT_REL_DEFAULT, ".fl_audit.log") == 0);
    ASSERT(strcmp(FL_AUDIT_ENV, "FL_AUDIT") == 0);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_ring_log_* (ring buffer, P6-2)
 * ---------------------------------------------------------------------- */

static int test_ring_log_append_and_copy_out(void) {
    fl_ring_log_reset();
    fl_ring_log_append_line("hello-ring");
    char buf[256];
    size_t n = fl_ring_log_copy_out(buf, sizeof buf);
    ASSERT(n > 0u);
    ASSERT(strstr(buf, "hello-ring") != NULL);
    return 0;
}

static int test_ring_log_append_multiple(void) {
    fl_ring_log_reset();
    fl_ring_log_append_line("line-alpha");
    fl_ring_log_append_line("line-beta");
    fl_ring_log_append_line("line-gamma");
    char buf[1024];
    size_t n = fl_ring_log_copy_out(buf, sizeof buf);
    ASSERT(n > 0u);
    ASSERT(strstr(buf, "line-alpha") != NULL);
    ASSERT(strstr(buf, "line-beta") != NULL);
    ASSERT(strstr(buf, "line-gamma") != NULL);
    return 0;
}

static int test_ring_log_reset_clears_content(void) {
    fl_ring_log_reset();
    fl_ring_log_append_line("before-reset");
    fl_ring_log_reset();
    char buf[256];
    size_t n = fl_ring_log_copy_out(buf, sizeof buf);
    ASSERT(n == 0u);
    ASSERT(buf[0] == '\0');
    return 0;
}

static int test_ring_log_drop_count_initially_zero(void) {
    fl_ring_log_reset();
    ASSERT(fl_ring_log_drop_count() == 0u);
    return 0;
}

static int test_ring_log_single_line_too_large_drops(void) {
    fl_ring_log_reset();
    unsigned before = fl_ring_log_drop_count();
    /* A line larger than FL_RING_LOG_CAPACITY is rejected immediately (add > cap). */
    char *big = (char *)malloc(FL_RING_LOG_CAPACITY + 2u);
    ASSERT(big != NULL);
    memset(big, 'A', FL_RING_LOG_CAPACITY + 1u);
    big[FL_RING_LOG_CAPACITY + 1u] = '\0';
    fl_ring_log_append_line(big);
    free(big);
    unsigned after = fl_ring_log_drop_count();
    ASSERT(after > before);
    return 0;
}

static int test_ring_log_overflow_evicts_oldest(void) {
    fl_ring_log_reset();
    /* Write lines totalling more than FL_RING_LOG_CAPACITY to force eviction.
     * 100 lines of 127 chars = ~12700 bytes > 8192. Oldest lines are evicted to
     * make room; the ring must not crash and copy_out must produce valid NUL-terminated output. */
    char line[128];
    memset(line, 'y', sizeof line - 1u);
    line[sizeof line - 1u] = '\0';
    for (int i = 0; i < 100; i++)
        fl_ring_log_append_line(line);
    char buf[FL_RING_LOG_CAPACITY + 2u];
    size_t n = fl_ring_log_copy_out(buf, sizeof buf);
    ASSERT(n < sizeof buf);
    ASSERT(buf[n] == '\0');
    return 0;
}

static int test_ring_log_null_append_safe(void) {
    fl_ring_log_reset();
    fl_ring_log_append_line(NULL);   /* must not crash */
    ASSERT(fl_ring_log_drop_count() == 0u);
    return 0;
}

static int test_ring_log_copy_out_null_buf_safe(void) {
    fl_ring_log_reset();
    fl_ring_log_append_line("test");
    size_t n = fl_ring_log_copy_out(NULL, 128u);
    ASSERT(n == 0u);
    return 0;
}

static int test_ring_log_copy_out_zero_cap_safe(void) {
    fl_ring_log_reset();
    fl_ring_log_append_line("test");
    char buf[4] = "xxx";
    size_t n = fl_ring_log_copy_out(buf, 0u);
    ASSERT(n == 0u);
    return 0;
}

static int test_ring_log_copy_out_truncates_to_cap(void) {
    fl_ring_log_reset();
    fl_ring_log_append_line("abcdefghij");
    char buf[5];
    memset(buf, 0xFF, sizeof buf);
    size_t n = fl_ring_log_copy_out(buf, sizeof buf);
    ASSERT(n < sizeof buf);
    ASSERT(buf[n] == '\0');
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_audit_authz_event()
 * ---------------------------------------------------------------------- */

static int test_audit_authz_event_noop_when_env_unset(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);
    fl_ring_log_reset();

    unsetenv(FL_AUDIT_ENV);
    fl_audit_authz_event("some-cmd", 1u, 1);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp == NULL);

    leave_tmpdir();
    return 0;
}

static int test_audit_authz_event_noop_null_cmd(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);
    fl_ring_log_reset();

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_authz_event(NULL, 1u, 1);   /* must not crash */
    unsetenv(FL_AUDIT_ENV);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp == NULL);

    leave_tmpdir();
    return 0;
}

static int test_audit_authz_event_writes_deny(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_authz_event("format disk", 5u, 1);
    unsetenv(FL_AUDIT_ENV);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    ASSERT(strstr(buf, "type=authz") != NULL);
    ASSERT(strstr(buf, "denied=1") != NULL);
    ASSERT(strstr(buf, "cmd_no=5") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_authz_event_writes_allow(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_authz_event("version", 0u, 0);
    unsetenv(FL_AUDIT_ENV);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    ASSERT(strstr(buf, "type=authz") != NULL);
    ASSERT(strstr(buf, "denied=0") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_authz_event_records_bundle_rev(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_authz_event("ls", 2u, 0);
    unsetenv(FL_AUDIT_ENV);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    char expected_bundle[32];
    snprintf(expected_bundle, sizeof expected_bundle, "bundle=%d", FL_CONTRACT_BUNDLE_REV);
    ASSERT(strstr(buf, expected_bundle) != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_authz_event_appends_to_ring(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);
    fl_ring_log_reset();

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_authz_event("ping", 3u, 1);
    unsetenv(FL_AUDIT_ENV);

    char buf[256];
    size_t n = fl_ring_log_copy_out(buf, sizeof buf);
    ASSERT(n > 0u);
    ASSERT(strstr(buf, "type=authz") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: contract constants (P0-1/P0-2 header additions)
 * ---------------------------------------------------------------------- */

static int test_contract_bundle_rev(void) {
    ASSERT(FL_CONTRACT_BUNDLE_REV == 4);
    return 0;
}

static int test_contract_surface_enum(void) {
    ASSERT((int)FL_CONTRACT_SURFACE_DRIVER_OPS == 0);
    ASSERT((int)FL_CONTRACT_SURFACE_NETDEV     == 1);
    ASSERT((int)FL_CONTRACT_SURFACE_LOG_SINK   == 2);
    ASSERT((int)FL_CONTRACT_SURFACE_AUTHZ      == 3);
    ASSERT((int)FL_CONTRACT_SURFACE_FS_JAIL    == 4);
    ASSERT((int)FL_CONTRACT_SURFACE_COUNT      == 5);
    return 0;
}

static int test_result_probe_skip_equals_nosys(void) {
    ASSERT(FL_RESULT_PROBE_SKIP == FL_RESULT_NOSYS);
    ASSERT(FL_RESULT_PROBE_SKIP == -38);
    return 0;
}

static int test_result_json_rc_bounds(void) {
    ASSERT(FL_RESULT_JSON_RC_MIN < 0);
    ASSERT(FL_RESULT_JSON_RC_MAX > 0);
    ASSERT(FL_RESULT_OK    >= FL_RESULT_JSON_RC_MIN && FL_RESULT_OK    <= FL_RESULT_JSON_RC_MAX);
    ASSERT(FL_RESULT_ERR   >= FL_RESULT_JSON_RC_MIN && FL_RESULT_ERR   <= FL_RESULT_JSON_RC_MAX);
    ASSERT(FL_RESULT_INVAL >= FL_RESULT_JSON_RC_MIN && FL_RESULT_INVAL <= FL_RESULT_JSON_RC_MAX);
    ASSERT(FL_RESULT_NOSYS >= FL_RESULT_JSON_RC_MIN && FL_RESULT_NOSYS <= FL_RESULT_JSON_RC_MAX);
    return 0;
}

static int test_log_facility_constants(void) {
    ASSERT(FL_LOG_FACILITY_USER  == 1);
    ASSERT(FL_LOG_FACILITY_AUDIT == 9);
    return 0;
}

static int test_ring_log_capacity_constant(void) {
    ASSERT(FL_RING_LOG_CAPACITY == 8192u);
    return 0;
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(void) {
    if (make_tmpdir() != 0) {
        fprintf(stderr, "Failed to create temp directory.\n");
        return 1;
    }

    printf("test_audit_show_no_file... ");
    if (test_audit_show_no_file() != 0) return 1;
    printf("OK\n");

    printf("test_audit_show_empty_file... ");
    if (test_audit_show_empty_file() != 0) return 1;
    printf("OK\n");

    printf("test_audit_show_n_clamping... ");
    if (test_audit_show_n_clamping() != 0) return 1;
    printf("OK\n");

    printf("test_audit_show_returns_last_n... ");
    if (test_audit_show_returns_last_n() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_noop_when_env_unset... ");
    if (test_audit_completed_noop_when_env_unset() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_noop_env_zero... ");
    if (test_audit_completed_noop_env_zero() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_noop_null_cmd... ");
    if (test_audit_completed_noop_null_cmd() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_writes_file... ");
    if (test_audit_completed_writes_file() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_nonzero_exit_maps_to_err... ");
    if (test_audit_completed_nonzero_exit_maps_to_err() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_sanitizes_control_chars... ");
    if (test_audit_completed_sanitizes_control_chars() != 0) return 1;
    printf("OK\n");

    printf("test_audit_sink_invoked... ");
    if (test_audit_sink_invoked() != 0) return 1;
    printf("OK\n");

    printf("test_audit_sink_null_safe... ");
    if (test_audit_sink_null_safe() != 0) return 1;
    printf("OK\n");

    printf("test_log_level_values... ");
    if (test_log_level_values() != 0) return 1;
    printf("OK\n");

    printf("test_authz_decisions... ");
    if (test_authz_decisions() != 0) return 1;
    printf("OK\n");

    printf("test_log_sink_ops_struct... ");
    if (test_log_sink_ops_struct() != 0) return 1;
    printf("OK\n");

    printf("test_log_dispatch_rate_limit... ");
    if (test_log_dispatch_rate_limit() != 0) return 1;
    printf("OK\n");

    printf("test_log_dispatch_null_sink... ");
    if (test_log_dispatch_null_sink() != 0) return 1;
    printf("OK\n");

    printf("test_audit_constants... ");
    if (test_audit_constants() != 0) return 1;
    printf("OK\n");

    /* --- Ring buffer (P6-2) --- */
    printf("test_ring_log_append_and_copy_out... ");
    if (test_ring_log_append_and_copy_out() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_append_multiple... ");
    if (test_ring_log_append_multiple() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_reset_clears_content... ");
    if (test_ring_log_reset_clears_content() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_drop_count_initially_zero... ");
    if (test_ring_log_drop_count_initially_zero() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_single_line_too_large_drops... ");
    if (test_ring_log_single_line_too_large_drops() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_overflow_evicts_oldest... ");
    if (test_ring_log_overflow_evicts_oldest() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_null_append_safe... ");
    if (test_ring_log_null_append_safe() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_copy_out_null_buf_safe... ");
    if (test_ring_log_copy_out_null_buf_safe() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_copy_out_zero_cap_safe... ");
    if (test_ring_log_copy_out_zero_cap_safe() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_copy_out_truncates_to_cap... ");
    if (test_ring_log_copy_out_truncates_to_cap() != 0) return 1;
    printf("OK\n");

    /* --- fl_audit_authz_event() --- */
    printf("test_audit_authz_event_noop_when_env_unset... ");
    if (test_audit_authz_event_noop_when_env_unset() != 0) return 1;
    printf("OK\n");

    printf("test_audit_authz_event_noop_null_cmd... ");
    if (test_audit_authz_event_noop_null_cmd() != 0) return 1;
    printf("OK\n");

    printf("test_audit_authz_event_writes_deny... ");
    if (test_audit_authz_event_writes_deny() != 0) return 1;
    printf("OK\n");

    printf("test_audit_authz_event_writes_allow... ");
    if (test_audit_authz_event_writes_allow() != 0) return 1;
    printf("OK\n");

    printf("test_audit_authz_event_records_bundle_rev... ");
    if (test_audit_authz_event_records_bundle_rev() != 0) return 1;
    printf("OK\n");

    printf("test_audit_authz_event_appends_to_ring... ");
    if (test_audit_authz_event_appends_to_ring() != 0) return 1;
    printf("OK\n");

    /* --- Contract constants (P0-1/P0-2) --- */
    printf("test_contract_bundle_rev... ");
    if (test_contract_bundle_rev() != 0) return 1;
    printf("OK\n");

    printf("test_contract_surface_enum... ");
    if (test_contract_surface_enum() != 0) return 1;
    printf("OK\n");

    printf("test_result_probe_skip_equals_nosys... ");
    if (test_result_probe_skip_equals_nosys() != 0) return 1;
    printf("OK\n");

    printf("test_result_json_rc_bounds... ");
    if (test_result_json_rc_bounds() != 0) return 1;
    printf("OK\n");

    printf("test_log_facility_constants... ");
    if (test_log_facility_constants() != 0) return 1;
    printf("OK\n");

    printf("test_ring_log_capacity_constant... ");
    if (test_ring_log_capacity_constant() != 0) return 1;
    printf("OK\n");

    rmrf_dir(g_tmpdir);
    printf("All audit log tests passed.\n");
    return 0;
}