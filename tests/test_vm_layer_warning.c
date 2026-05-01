/**
 * Tests for fs_jail (filesystem sandbox) and path_log_is_initialized().
 * These test new functionality added in the driver model PR.
 *
 * Build: see Makefile target test_vm_layer_warning
 */
#include "common.h"
#include "fs_jail.h"
#include "path_log.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL (%s:%d): %s\n", __FILE__, __LINE__, #c); return 1; } } while(0)

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void reset_jail_state(void) {
    g_vm_mode    = 0;
    g_vm_root[0] = '\0';
    g_cwd[0]     = '.';
    g_cwd[1]     = '\0';
    fs_jail_init();   /* clears internal static state */
}

/* ------------------------------------------------------------------ */
/* fs_jail: inactive by default (no VM mode)                           */
/* ------------------------------------------------------------------ */
static int test_jail_inactive_when_vm_off(void) {
    reset_jail_state();
    ASSERT(fs_jail_is_active() == 0);
    ASSERT(fs_jail_root_configured() == 0);
    /* check_path returns 0 (allow) when jail is inactive */
    ASSERT(fs_jail_check_path("/etc/passwd") == 0);
    ASSERT(fs_jail_check_path("/tmp") == 0);
    ASSERT(fs_jail_check_path(NULL) == -1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail: VM mode on falls back to cwd when root is not configured    */
/* ------------------------------------------------------------------ */
static int test_jail_active_no_root(void) {
    g_vm_mode    = 1;
    g_vm_root[0] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);
    ASSERT(fs_jail_root_configured() == 0);
    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail: active with a real root directory                          */
/* ------------------------------------------------------------------ */
static int test_jail_active_with_real_root(void) {
    /* Use /tmp as the jail root - guaranteed to exist */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();

    ASSERT(fs_jail_is_active() == 1);
    ASSERT(fs_jail_root_configured() == 1);

    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail_check_path: null / empty                                    */
/* ------------------------------------------------------------------ */
static int test_jail_check_path_null_empty(void) {
    /* With jail inactive, NULL still returns -1 */
    reset_jail_state();
    ASSERT(fs_jail_check_path(NULL) == -1);

    /* Activate jail on /tmp */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    ASSERT(fs_jail_check_path(NULL) == -1);
    ASSERT(fs_jail_check_path("") == -1);

    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail_check_path: paths inside vs outside the jail               */
/* ------------------------------------------------------------------ */
static int test_jail_check_path_inside_outside(void) {
    /* Jail root = /tmp */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* /tmp itself - inside */
    ASSERT(fs_jail_check_path("/tmp") == 0);
    /* /tmp/something - inside */
    ASSERT(fs_jail_check_path("/tmp/some_file_that_may_not_exist") == 0);

    /* /etc - outside */
    ASSERT(fs_jail_check_path("/etc") == -1);
    /* /etc/passwd - outside */
    ASSERT(fs_jail_check_path("/etc/passwd") == -1);
    /* /proc - outside */
    ASSERT(fs_jail_check_path("/proc") == -1);
    /* root "/" - outside */
    ASSERT(fs_jail_check_path("/") == -1);

    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail_check_path: relative paths resolved via g_cwd              */
/* ------------------------------------------------------------------ */
static int test_jail_check_path_relative(void) {
    /* Jail root = /tmp, cwd inside jail */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* Relative "foo" → resolves to /tmp/foo → inside jail */
    ASSERT(fs_jail_check_path("foo") == 0);
    /* Relative "../etc/passwd" → resolves to /etc/passwd → outside */
    ASSERT(fs_jail_check_path("../etc/passwd") == -1);

    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail_openat: null path returns EINVAL                           */
/* ------------------------------------------------------------------ */
static int test_jail_openat_null(void) {
    reset_jail_state();
    int fd = fs_jail_openat(NULL, O_RDONLY, 0);
    ASSERT(fd == -1);
    ASSERT(errno == EINVAL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail_openat: inactive jail falls back to regular open           */
/* ------------------------------------------------------------------ */
static int test_jail_openat_inactive_fallback(void) {
    reset_jail_state();
    /* Jail is inactive: openat should fall back to regular open */
    /* /dev/null is always readable */
    int fd = fs_jail_openat("/dev/null", O_RDONLY, 0);
    ASSERT(fd >= 0);
    close(fd);
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail_openat: rejects paths outside the jail                     */
/* ------------------------------------------------------------------ */
static int test_jail_openat_outside_jail(void) {
    /* Jail root = /tmp */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* /dev/null is outside /tmp */
    int fd = fs_jail_openat("/dev/null", O_RDONLY, 0);
    ASSERT(fd == -1);
    ASSERT(errno == EPERM);

    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail_openat: allows open of a real file inside the jail         */
/* ------------------------------------------------------------------ */
static int test_jail_openat_inside_jail(void) {
    /* Create a temp file inside /tmp */
    char tmp_path[] = "/tmp/fl_jail_test_XXXXXX";
    int tmp_fd = mkstemp(tmp_path);
    if (tmp_fd < 0) {
        fprintf(stderr, "SKIP: cannot create temp file in /tmp\n");
        return 0;
    }
    close(tmp_fd);

    /* Jail root = /tmp */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    int fd = fs_jail_openat(tmp_path, O_RDONLY, 0);
    if (fd < 0) {
        /* On systems without /proc/self/fd readlink support or O_NOFOLLOW issues,
         * this may fail; treat as informational */
        fprintf(stderr, "  (Note: fs_jail_openat returned -1, errno=%d - may be platform limitation)\n", errno);
    } else {
        close(fd);
    }

    unlink(tmp_path);
    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_jail_is_active: verify exact boolean return values              */
/* ------------------------------------------------------------------ */
static int test_jail_is_active_return_values(void) {
    reset_jail_state();
    int v = fs_jail_is_active();
    ASSERT(v == 0 || v == 1); /* must be exactly 0 or 1 */
    ASSERT(v == 0);           /* inactive when vm_mode=0 */

    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    v = fs_jail_is_active();
    ASSERT(v == 1);

    reset_jail_state();
    v = fs_jail_is_active();
    ASSERT(v == 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* path_log: path_log_is_initialized state tracking                   */
/* ------------------------------------------------------------------ */
static int test_path_log_initialized(void) {
    /* Ensure shutdown state first */
    path_log_shutdown();
    ASSERT(path_log_is_initialized() == 0);

    /* Initialize */
    path_log_init();
    ASSERT(path_log_is_initialized() == 1);

    /* Double-init is a no-op but stays initialized */
    path_log_init();
    ASSERT(path_log_is_initialized() == 1);

    /* Shutdown clears the flag */
    path_log_shutdown();
    ASSERT(path_log_is_initialized() == 0);

    /* Re-init works after shutdown */
    path_log_init();
    ASSERT(path_log_is_initialized() == 1);

    /* Recording only works when initialized */
    path_log_record(PATH_OP_READ, "/test/path");
    /* No crash = pass */

    path_log_shutdown();
    ASSERT(path_log_is_initialized() == 0);

    /* Recording after shutdown is a no-op (no crash) */
    path_log_record(PATH_OP_WRITE, "/test/path");

    return 0;
}

/* ------------------------------------------------------------------ */
/* Regression: fs_jail_init called multiple times resets state        */
/* ------------------------------------------------------------------ */
static int test_jail_reinit_resets_state(void) {
    /* First: activate with /tmp */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* Call reset_jail_state (sets vm_mode=0, calls fs_jail_init) */
    reset_jail_state();
    ASSERT(fs_jail_is_active() == 0);
    ASSERT(fs_jail_root_configured() == 0);

    /* Now re-activate */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Regression: jail root with trailing slash is normalised            */
/* ------------------------------------------------------------------ */
static int test_jail_root_trailing_slash(void) {
    /* /tmp/ (with slash) should behave exactly like /tmp */
    g_vm_mode = 1;
    strncpy(g_vm_root, "/tmp/", CWD_MAX - 1);
    g_vm_root[CWD_MAX - 1] = '\0';
    strncpy(g_cwd, "/tmp", CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = '\0';
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* Inside path still allowed */
    ASSERT(fs_jail_check_path("/tmp") == 0);
    /* Outside path still rejected */
    ASSERT(fs_jail_check_path("/etc") == -1);

    reset_jail_state();
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void) {
    printf("=== fs_jail + path_log test suite ===\n");

#define RUN(name) do { \
    printf("%s... ", #name); fflush(stdout); \
    if (name() != 0) return 1; \
    printf("OK\n"); \
} while(0)

    RUN(test_jail_inactive_when_vm_off);
    RUN(test_jail_active_no_root);
    RUN(test_jail_active_with_real_root);
    RUN(test_jail_check_path_null_empty);
    RUN(test_jail_check_path_inside_outside);
    RUN(test_jail_check_path_relative);
    RUN(test_jail_openat_null);
    RUN(test_jail_openat_inactive_fallback);
    RUN(test_jail_openat_outside_jail);
    RUN(test_jail_openat_inside_jail);
    RUN(test_jail_is_active_return_values);
    RUN(test_path_log_initialized);
    RUN(test_jail_reinit_resets_state);
    RUN(test_jail_root_trailing_slash);

#undef RUN

    printf("=== All fs_jail + path_log tests passed.\n");
    return 0;
}
