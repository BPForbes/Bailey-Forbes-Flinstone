/**
 * Property tests: path normalization, directory entry validity, cluster invariants.
 */
#include "util.h"
#include "common.h"
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

    printf("All invariant tests passed.\n");
    return 0;
}
