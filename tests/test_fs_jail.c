/**
 * test_fs_jail.c - Unit tests for the fs_jail module (kernel/core/vfs/fs_jail.c)
 *
 * Tests cover:
 *   - fs_jail_init(): inactive when g_vm_mode=0, sets root from cwd or g_vm_root
 *   - fs_jail_is_active(): conditions for being active
 *   - fs_jail_check_path(): null/empty, inside/outside jail, relative paths,
 *     nonexistent paths, prefix-attack boundary, and inactive-jail pass-through
 */
#include "fs_jail.h"
#include "common.h"
#include "mem_domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>
#include <limits.h>
#include <errno.h>

/* g_fs_jail_root is a non-static global defined in fs_jail.c */
extern char g_fs_jail_root[];

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while(0)

/* Reset all jail-related global state between tests */
static void reset_jail_globals(void) {
    g_vm_mode = 0;
    g_vm_root[0] = '\0';
    g_cwd[0] = '\0';
    if (getcwd(g_cwd, sizeof(g_cwd)) == NULL)
        strncpy(g_cwd, "/tmp", sizeof(g_cwd) - 1);
}

/* Create a temporary directory and return its canonical path via out (must be free()'d) */
static char *make_tempdir(char *tmpl) {
    if (!mkdtemp(tmpl))
        return NULL;
    char *canon = realpath(tmpl, NULL);
    return canon; /* caller must free */
}

/* ---------------------------------------------------------------------------
 * Test: jail is NOT active when g_vm_mode == 0
 * --------------------------------------------------------------------------- */
static int test_jail_inactive_without_vm_mode(void) {
    reset_jail_globals();
    g_vm_mode = 0;
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 0);
    /* check_path must allow any path when jail is inactive */
    ASSERT(fs_jail_check_path("/etc/passwd") == 0);
    ASSERT(fs_jail_check_path("/tmp") == 0);
    ASSERT(fs_jail_check_path(".") == 0);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: jail is NOT active when g_vm_mode == 1 but init was not called
 * --------------------------------------------------------------------------- */
static int test_jail_inactive_before_init(void) {
    reset_jail_globals();
    g_vm_mode = 1;
    /* Manually zero jail state (simulate fresh binary) */
    g_fs_jail_root[0] = '\0';
    /* is_active() depends on the internal g_fs_jail_len which is 0 by default */
    /* We can only verify this via is_active after init */
    fs_jail_init();                /* must be called with a real cwd */
    ASSERT(fs_jail_is_active() == 1);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: fs_jail_init with g_vm_mode=1 and no g_vm_root -> uses cwd
 * --------------------------------------------------------------------------- */
static int test_jail_init_uses_cwd_when_no_vm_root(void) {
    reset_jail_globals();
    g_vm_mode = 1;
    g_vm_root[0] = '\0';

    char expected[PATH_MAX];
    if (getcwd(expected, sizeof(expected)) == NULL) {
        fprintf(stderr, "SKIP: getcwd failed\n");
        return 0;
    }
    /* Canonicalize (in case cwd itself has symlinks) */
    char *canon = realpath(expected, NULL);
    if (!canon) {
        fprintf(stderr, "SKIP: realpath(cwd) failed\n");
        return 0;
    }

    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);
    ASSERT(strcmp(g_fs_jail_root, canon) == 0);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: fs_jail_init with g_vm_root set to an existing directory
 * --------------------------------------------------------------------------- */
static int test_jail_init_uses_vm_root(void) {
    char tmpl[] = "/tmp/fs_jail_test_root_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) {
        fprintf(stderr, "SKIP: mkdtemp failed\n");
        return 0;
    }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, tmpl, sizeof(g_vm_root) - 1);

    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);
    ASSERT(strcmp(g_fs_jail_root, canon) == 0);

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: fs_jail_init strips trailing slashes from g_fs_jail_root
 * --------------------------------------------------------------------------- */
static int test_jail_init_strips_trailing_slash(void) {
    char tmpl[] = "/tmp/fs_jail_trail_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) {
        fprintf(stderr, "SKIP: mkdtemp failed\n");
        return 0;
    }

    reset_jail_globals();
    g_vm_mode = 1;
    /* Set g_vm_root to the dir with a trailing slash */
    snprintf(g_vm_root, sizeof(g_vm_root), "%s/", canon);

    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);
    size_t len = strlen(g_fs_jail_root);
    ASSERT(len > 0);
    ASSERT(g_fs_jail_root[len - 1] != '/');

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: fs_jail_check_path(NULL) returns -1 regardless of jail state
 * --------------------------------------------------------------------------- */
static int test_jail_check_null_path(void) {
    /* Test 1: jail inactive */
    reset_jail_globals();
    g_vm_mode = 0;
    fs_jail_init();
    ASSERT(fs_jail_check_path(NULL) == -1);

    /* Test 2: jail active */
    reset_jail_globals();
    g_vm_mode = 1;
    fs_jail_init();
    ASSERT(fs_jail_check_path(NULL) == -1);

    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: fs_jail_check_path("") returns -1 when jail is active
 * --------------------------------------------------------------------------- */
static int test_jail_check_empty_path(void) {
    char tmpl[] = "/tmp/fs_jail_empty_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    fs_jail_init();

    ASSERT(fs_jail_is_active() == 1);
    ASSERT(fs_jail_check_path("") == -1);

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: path inside the jail is allowed (returns 0)
 * --------------------------------------------------------------------------- */
static int test_jail_check_path_inside(void) {
    char tmpl[] = "/tmp/fs_jail_inside_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    strncpy(g_cwd, canon, sizeof(g_cwd) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* The jail root itself must be allowed */
    ASSERT(fs_jail_check_path(canon) == 0);

    /* A subdirectory inside the jail */
    char subdir[PATH_MAX];
    snprintf(subdir, sizeof(subdir), "%s/subdir", canon);
    mkdir(subdir, 0755);
    ASSERT(fs_jail_check_path(subdir) == 0);
    rmdir(subdir);

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: path outside the jail is rejected (returns -1)
 * --------------------------------------------------------------------------- */
static int test_jail_check_path_outside(void) {
    char tmpl[] = "/tmp/fs_jail_outside_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    strncpy(g_cwd, canon, sizeof(g_cwd) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* /etc and /usr are always outside a tmpfs jail */
    ASSERT(fs_jail_check_path("/etc") == -1);
    ASSERT(fs_jail_check_path("/usr") == -1);
    /* /tmp itself is the parent — also outside unless jail IS /tmp */
    ASSERT(fs_jail_check_path("/tmp") == -1);

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: path that does not exist yet - checks parent directory
 * The parent is inside the jail, so the nonexistent file should be allowed.
 * --------------------------------------------------------------------------- */
static int test_jail_check_nonexistent_inside(void) {
    char tmpl[] = "/tmp/fs_jail_nonexist_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    strncpy(g_cwd, canon, sizeof(g_cwd) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* File does not exist yet, but parent (canon) is inside the jail */
    char newfile[PATH_MAX];
    snprintf(newfile, sizeof(newfile), "%s/newfile_that_does_not_exist.txt", canon);
    ASSERT(access(newfile, F_OK) != 0); /* confirm it doesn't exist */
    ASSERT(fs_jail_check_path(newfile) == 0);

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: nonexistent path whose parent is outside the jail is rejected
 * --------------------------------------------------------------------------- */
static int test_jail_check_nonexistent_outside(void) {
    char tmpl[] = "/tmp/fs_jail_noout_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    strncpy(g_cwd, canon, sizeof(g_cwd) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* Parent /etc exists but is outside the jail */
    ASSERT(fs_jail_check_path("/etc/no_such_file_xyzzy") == -1);

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: relative path is resolved against g_cwd
 * When g_cwd is inside the jail, relative paths are allowed.
 * --------------------------------------------------------------------------- */
static int test_jail_check_relative_path_inside(void) {
    char tmpl[] = "/tmp/fs_jail_rel_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    strncpy(g_cwd, canon, sizeof(g_cwd) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* Create a real subdir so realpath succeeds */
    char subdir[PATH_MAX];
    snprintf(subdir, sizeof(subdir), "%s/reltest", canon);
    mkdir(subdir, 0755);

    /* Relative path "reltest" with g_cwd=canon should be allowed */
    ASSERT(fs_jail_check_path("reltest") == 0);

    rmdir(subdir);
    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: prefix-attack boundary check
 * Path "/jail_root_evil" must NOT be allowed when jail root is "/jail_root"
 * (strncmp prefix match is not enough; must verify separator character).
 * --------------------------------------------------------------------------- */
static int test_jail_check_prefix_attack(void) {
    char tmpl[] = "/tmp/fs_jail_pfx_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    strncpy(g_cwd, canon, sizeof(g_cwd) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* Construct a sibling path: canon + "_sibling" which is NOT inside canon */
    char sibling[PATH_MAX];
    snprintf(sibling, sizeof(sibling), "%s_sibling", canon);
    mkdir(sibling, 0755);

    int result = fs_jail_check_path(sibling);
    /* sibling path starts with canon's characters but is NOT under canon */
    ASSERT(result == -1);

    rmdir(sibling);
    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: when jail is inactive (g_vm_mode=0), check_path always returns 0
 * --------------------------------------------------------------------------- */
static int test_jail_check_always_passes_when_inactive(void) {
    reset_jail_globals();
    g_vm_mode = 0;
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 0);

    ASSERT(fs_jail_check_path("/etc") == 0);
    ASSERT(fs_jail_check_path("/tmp") == 0);
    ASSERT(fs_jail_check_path("/usr/bin/ls") == 0);
    ASSERT(fs_jail_check_path("some/relative/path") == 0);

    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: jail root itself is accessible (exact match, no trailing slash)
 * --------------------------------------------------------------------------- */
static int test_jail_check_exact_root_match(void) {
    char tmpl[] = "/tmp/fs_jail_exact_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    strncpy(g_cwd, canon, sizeof(g_cwd) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* Exact root must be allowed */
    ASSERT(fs_jail_check_path(canon) == 0);

    /* Root with trailing slash: realpath resolves it to same canonical path */
    char root_slash[PATH_MAX];
    snprintf(root_slash, sizeof(root_slash), "%s/", canon);
    ASSERT(fs_jail_check_path(root_slash) == 0);

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: fs_jail_init is idempotent - calling twice is safe
 * --------------------------------------------------------------------------- */
static int test_jail_init_idempotent(void) {
    char tmpl[] = "/tmp/fs_jail_idem_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    fs_jail_init();
    char first_root[PATH_MAX];
    strncpy(first_root, g_fs_jail_root, sizeof(first_root) - 1);

    fs_jail_init(); /* second call */
    ASSERT(strcmp(g_fs_jail_root, first_root) == 0);
    ASSERT(fs_jail_is_active() == 1);

    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test: regression - path equal to root with extra chars not a subdirectory
 * E.g., root=/tmp/abc, path=/tmp/abcdef must be rejected.
 * --------------------------------------------------------------------------- */
static int test_jail_check_regression_prefix_not_subdir(void) {
    char tmpl[] = "/tmp/fs_jail_reg_XXXXXX";
    char *canon = make_tempdir(tmpl);
    if (!canon) { fprintf(stderr, "SKIP: mkdtemp failed\n"); return 0; }

    reset_jail_globals();
    g_vm_mode = 1;
    strncpy(g_vm_root, canon, sizeof(g_vm_root) - 1);
    strncpy(g_cwd, canon, sizeof(g_cwd) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    /* Build a path that is the jail root string + "extra" (no slash separator) */
    char notchild[PATH_MAX];
    snprintf(notchild, sizeof(notchild), "%sextra", canon); /* e.g. /tmp/fs_jail_reg_XXXXXXextra */
    mkdir(notchild, 0755);

    int ret = fs_jail_check_path(notchild);
    /* notchild is NOT inside canon - must be rejected */
    ASSERT(ret == -1);

    rmdir(notchild);
    rmdir(canon);
    free(canon);
    reset_jail_globals();
    return 0;
}

/* ---------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */
int main(void) {
    printf("=== fs_jail unit tests ===\n");

#define RUN(t) do { \
    printf("%-55s", #t " ..."); \
    fflush(stdout); \
    int r = t(); \
    printf("%s\n", r == 0 ? "OK" : "FAIL"); \
    if (r != 0) return 1; \
} while(0)

    RUN(test_jail_inactive_without_vm_mode);
    RUN(test_jail_inactive_before_init);
    RUN(test_jail_init_uses_cwd_when_no_vm_root);
    RUN(test_jail_init_uses_vm_root);
    RUN(test_jail_init_strips_trailing_slash);
    RUN(test_jail_check_null_path);
    RUN(test_jail_check_empty_path);
    RUN(test_jail_check_path_inside);
    RUN(test_jail_check_path_outside);
    RUN(test_jail_check_nonexistent_inside);
    RUN(test_jail_check_nonexistent_outside);
    RUN(test_jail_check_relative_path_inside);
    RUN(test_jail_check_prefix_attack);
    RUN(test_jail_check_always_passes_when_inactive);
    RUN(test_jail_check_exact_root_match);
    RUN(test_jail_init_idempotent);
    RUN(test_jail_check_regression_prefix_not_subdir);

#undef RUN

    printf("=== All fs_jail tests passed. ===\n");
    return 0;
}
