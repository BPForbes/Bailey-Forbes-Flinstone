/**
 * test_fs_helpers.c - the shared file helpers in kernel/core/vfs/server_shared_fs.c
 * (fl_fs_read_file) and the header-only fl_fs_basename (fl/path.h).
 */
#include "fl/path.h"
#include "server_shared_fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

static char g_dir[64];

static void path_in(char *out, size_t cap, const char *name) {
    snprintf(out, cap, "%s/%s", g_dir, name);
}

static int put(const char *name, const uint8_t *data, size_t len) {
    char path[128];
    FILE *f;

    path_in(path, sizeof path, name);
    f = fopen(path, "wb");
    if (!f)
        return -1;
    if (len && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    return fclose(f);
}

static int test_reads_whole_file(void) {
    uint8_t in[300];
    uint8_t *data = NULL;
    size_t len = 99, i;
    char path[128];

    for (i = 0; i < sizeof in; i++)
        in[i] = (uint8_t)(i * 7u); /* includes NUL bytes: binary safe */
    ASSERT(put("normal", in, sizeof in) == 0);
    path_in(path, sizeof path, "normal");
    ASSERT(fl_fs_read_file(path, 4096u, &data, &len) == 0);
    ASSERT(data != NULL && len == sizeof in && memcmp(data, in, len) == 0);
    free(data);
    return 0;
}

static int test_empty_file_gives_non_null_data(void) {
    uint8_t *data = NULL;
    size_t len = 99;
    char path[128];

    ASSERT(put("empty", NULL, 0) == 0);
    path_in(path, sizeof path, "empty");
    ASSERT(fl_fs_read_file(path, 16u, &data, &len) == 0);
    ASSERT(data != NULL && len == 0u);
    free(data);
    return 0;
}

static int test_exact_cap_and_one_over(void) {
    uint8_t in[64];
    uint8_t *data = NULL;
    size_t len = 99;
    char path[128];

    memset(in, 0x5a, sizeof in);
    ASSERT(put("cap", in, sizeof in) == 0);
    path_in(path, sizeof path, "cap");
    ASSERT(fl_fs_read_file(path, sizeof in, &data, &len) == 0);
    ASSERT(data != NULL && len == sizeof in);
    free(data);
    data = NULL;
    errno = 0;
    ASSERT(fl_fs_read_file(path, sizeof in - 1u, &data, &len) == -2);
    ASSERT(errno == EFBIG);
    ASSERT(data == NULL && len == 0u);
    ASSERT(fl_fs_read_file(path, 0u, &data, &len) == -2);
    return 0;
}

static int test_missing_file(void) {
    uint8_t *data = (uint8_t *)&data;
    size_t len = 99;
    char path[128];

    path_in(path, sizeof path, "absent");
    errno = 0;
    ASSERT(fl_fs_read_file(path, 16u, &data, &len) == -3);
    ASSERT(errno == ENOENT);
    ASSERT(data == NULL && len == 0u);
    return 0;
}

static int test_directory_and_fifo_are_refused(void) {
    uint8_t *data = (uint8_t *)&data;
    size_t len = 99;
    char path[128];

    ASSERT(fl_fs_read_file(g_dir, 4096u, &data, &len) == -1);
    ASSERT(data == NULL && len == 0u);
    path_in(path, sizeof path, "fifo");
    ASSERT(mkfifo(path, 0600) == 0);
    data = (uint8_t *)&data;
    ASSERT(fl_fs_read_file(path, 4096u, &data, &len) == -1); /* must not hang */
    ASSERT(data == NULL && len == 0u);
    unlink(path);
    return 0;
}

static int test_symlink_is_followed_and_device_refused(void) {
    uint8_t *data = NULL;
    size_t len = 0;
    char real[128], link[128];
    const uint8_t in[] = {7, 8, 9};

    ASSERT(put("target", in, sizeof in) == 0);
    path_in(real, sizeof real, "target");
    path_in(link, sizeof link, "link");
    ASSERT(symlink(real, link) == 0);
    ASSERT(fl_fs_read_file(link, 16u, &data, &len) == 0); /* user-named paths follow links */
    ASSERT(data != NULL && len == sizeof in && memcmp(data, in, len) == 0);
    free(data);
    if (access("/dev/zero", F_OK) == 0) { /* skip the device check where /dev/zero is absent */
        data = (uint8_t *)&data;
        ASSERT(fl_fs_read_file("/dev/zero", 16u, &data, &len) == -1); /* not a regular file */
        ASSERT(data == NULL && len == 0u);
    }
    return 0;
}

static int test_null_arguments(void) {
    uint8_t *data = NULL;
    size_t len = 99;

    ASSERT(fl_fs_read_file(NULL, 16u, &data, &len) == -1);
    ASSERT(data == NULL && len == 0u);
    ASSERT(fl_fs_read_file("/", 16u, NULL, &len) == -1);
    ASSERT(fl_fs_read_file("/", 16u, &data, NULL) == -1);
    return 0;
}

static int test_basename_no_slash_is_whole_input(void) {
    const char *p = "name.txt";

    ASSERT(fl_fs_basename(p) == p);
    ASSERT(strcmp(fl_fs_basename(""), "") == 0);
    return 0;
}

static int test_basename_takes_text_after_last_slash(void) {
    const char *p = "a/b/c.txt";

    ASSERT(strcmp(fl_fs_basename(p), "c.txt") == 0);
    ASSERT(fl_fs_basename(p) == p + 4);
    ASSERT(strcmp(fl_fs_basename("/b"), "b") == 0);
    ASSERT(strcmp(fl_fs_basename("a//b"), "b") == 0);
    return 0;
}

static int test_basename_trailing_slash_is_empty(void) {
    ASSERT(strcmp(fl_fs_basename("a/b/"), "") == 0);
    ASSERT(strcmp(fl_fs_basename("/"), "") == 0);
    return 0;
}

static int test_basename_null_is_empty(void) {
    ASSERT(strcmp(fl_fs_basename(NULL), "") == 0);
    return 0;
}

static int test_basename_dots_and_backslash_unchanged(void) {
    ASSERT(strcmp(fl_fs_basename("a/.."), "..") == 0);
    ASSERT(strcmp(fl_fs_basename("."), ".") == 0);
    ASSERT(strcmp(fl_fs_basename("a\\b"), "a\\b") == 0);
    return 0;
}

int main(void) {
    static const char *const names[] = {"normal", "empty", "cap", "target", "link", "fifo"};
    int rc = 0;
    size_t i;

    snprintf(g_dir, sizeof g_dir, "/tmp/fl_fs_helpers_XXXXXX");
    if (!mkdtemp(g_dir)) {
        perror("mkdtemp");
        return 1;
    }
    rc |= test_reads_whole_file();
    rc |= test_empty_file_gives_non_null_data();
    rc |= test_exact_cap_and_one_over();
    rc |= test_missing_file();
    rc |= test_directory_and_fifo_are_refused();
    rc |= test_symlink_is_followed_and_device_refused();
    rc |= test_null_arguments();
    rc |= test_basename_no_slash_is_whole_input();
    rc |= test_basename_takes_text_after_last_slash();
    rc |= test_basename_trailing_slash_is_empty();
    rc |= test_basename_null_is_empty();
    rc |= test_basename_dots_and_backslash_unchanged();
    for (i = 0; i < sizeof names / sizeof *names; i++) {
        char path[128];

        path_in(path, sizeof path, names[i]);
        unlink(path);
    }
    rmdir(g_dir);
    if (rc == 0)
        printf("test_fs_helpers: all tests passed\n");
    return rc;
}
