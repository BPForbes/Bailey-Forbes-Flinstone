/**
 * test_fs_helpers.c - the shared file helpers in kernel/core/vfs/server_shared_fs.c
 * (fl_fs_read_file, the contained calls, the local path walk) and the header-only
 * fl_fs_basename (fl/path.h).
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "common.h"
#include "fl/path.h"
#include "contract_p5_file_delivery.h"
#include "server_shared_fs.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <grp.h>
#include <sys/wait.h>

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

/* ---- contained access ------------------------------------------------------ */

static char g_land[128], g_out[128];

static int rm_cb(const char *p, const struct stat *sb, int flag, struct FTW *f) {
    (void)sb; (void)flag; (void)f;
    return remove(p);
}

static int exists(const char *dir, const char *name) {
    char path[300];
    struct stat st;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    return lstat(path, &st) == 0;
}

static int slurp(const char *dir, const char *name, char *out, size_t cap) {
    char path[300];
    FILE *f;
    size_t n;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(out, 1, cap - 1u, f);
    out[n] = '\0';
    fclose(f);
    return (int)n;
}

/* Fresh landing and outside directories; g_cwd is the landing one. */
static int fresh_tree(void) {
    if (g_land[0]) {
        nftw(g_land, rm_cb, 8, FTW_DEPTH | FTW_PHYS);
        nftw(g_out, rm_cb, 8, FTW_DEPTH | FTW_PHYS);
    }
    snprintf(g_land, sizeof g_land, "%s/land", g_dir);
    snprintf(g_out, sizeof g_out, "%s/outside", g_dir);
    if (mkdir(g_land, 0700) != 0 || mkdir(g_out, 0700) != 0)
        return -1;
    snprintf(g_cwd, sizeof g_cwd, "%s", g_land);
    return 0;
}

static int put_in(const char *dir, const char *name, const char *text) {
    char path[300];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return -1;
    fputs(text, f);
    return fclose(f);
}

static int chmod_in_land(const char *name, mode_t mode) {
    char path[300];

    snprintf(path, sizeof path, "%s/%s", g_land, name);
    return chmod(path, mode);
}

static int test_contained_normal_write_and_replace(void) {
    const uint8_t a[] = "first", b[] = "second one";
    uint8_t *data = NULL;
    size_t len = 0;
    char buf[64], path[300];
    struct stat st;
    int dfd;

    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(fl_fs_write_file_atomic_in(dfd, "doc.txt", a, sizeof a - 1u) == 0);
    ASSERT(fl_fs_write_file_atomic_in(dfd, "doc.txt", b, sizeof b - 1u) == 0);
    ASSERT(slurp(g_land, "doc.txt", buf, sizeof buf) == (int)sizeof b - 1);
    ASSERT(strcmp(buf, "second one") == 0);
    ASSERT(!exists(g_land, "doc.txt.part"));
    snprintf(path, sizeof path, "%s/doc.txt", g_land);
    ASSERT(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    ASSERT(fl_fs_read_file_in(dfd, "doc.txt", 64u, &data, &len) == 0);
    ASSERT(data != NULL && len == sizeof b - 1u && memcmp(data, b, len) == 0);
    free(data);
    close(dfd);
    return 0;
}

static int test_contained_planted_part_link_is_not_written_through(void) {
    const uint8_t in[] = "new bytes";
    char link[300], target[300], buf[64];
    int dfd;

    ASSERT(fresh_tree() == 0);
    ASSERT(put_in(g_out, "victim", "original") == 0);
    snprintf(target, sizeof target, "%s/victim", g_out);
    snprintf(link, sizeof link, "%s/doc.txt.part", g_land);
    ASSERT(symlink(target, link) == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(fl_fs_write_file_atomic_in(dfd, "doc.txt", in, sizeof in - 1u) == 0);
    close(dfd);
    ASSERT(slurp(g_out, "victim", buf, sizeof buf) > 0 && strcmp(buf, "original") == 0);
    ASSERT(slurp(g_land, "doc.txt", buf, sizeof buf) > 0 && strcmp(buf, "new bytes") == 0);
    /* the path-based staging has the same rule */
    ASSERT(symlink(target, link) == 0);
    snprintf(link, sizeof link, "%s/doc.txt", g_land);
    ASSERT(fl_fs_write_file_atomic(link, in, sizeof in - 1u) == 0);
    ASSERT(slurp(g_out, "victim", buf, sizeof buf) > 0 && strcmp(buf, "original") == 0);
    return 0;
}

static int test_contained_link_at_destination_is_not_followed(void) {
    const uint8_t in[] = "payload";
    char link[300], target[300];
    int dfd;

    ASSERT(fresh_tree() == 0);
    snprintf(target, sizeof target, "%s/fresh", g_out);
    snprintf(link, sizeof link, "%s/dang", g_land);
    ASSERT(symlink(target, link) == 0); /* dangling */
    ASSERT(fl_server_shared_overwrite_local(NULL, "dang", in, sizeof in - 1u) != FL_RESULT_OK);
    ASSERT(!exists(g_out, "fresh"));
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(fl_fs_write_file_atomic_in(dfd, "dang", in, sizeof in - 1u) == 0);
    ASSERT(!exists(g_out, "fresh")); /* the link itself was replaced */
    close(dfd);
    /* a link to an existing file elsewhere */
    ASSERT(put_in(g_out, "keep", "untouched") == 0);
    snprintf(target, sizeof target, "%s/keep", g_out);
    snprintf(link, sizeof link, "%s/ex", g_land);
    ASSERT(symlink(target, link) == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "ex", in, sizeof in - 1u) != FL_RESULT_OK);
    {
        char buf[32];

        ASSERT(slurp(g_out, "keep", buf, sizeof buf) > 0 && strcmp(buf, "untouched") == 0);
    }
    return 0;
}

static int test_contained_read_refuses_link(void) {
    uint8_t *data = (uint8_t *)&data;
    size_t len = 9;
    char link[300], target[300];
    int dfd;

    ASSERT(fresh_tree() == 0);
    ASSERT(put_in(g_out, "secret_like", "abc") == 0);
    snprintf(target, sizeof target, "%s/secret_like", g_out);
    snprintf(link, sizeof link, "%s/l", g_land);
    ASSERT(symlink(target, link) == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    errno = 0;
    ASSERT(fl_fs_read_file_in(dfd, "l", 16u, &data, &len) == -3);
    ASSERT(errno == ELOOP && data == NULL && len == 0u);
    ASSERT(fl_fs_read_file_in(dfd, "absent", 16u, &data, &len) == -3);
    close(dfd);
    return 0;
}

static int test_contained_linked_subdirectory_is_refused(void) {
    const uint8_t in[] = "payload";
    char link[300];
    int dfd = -1;
    char leaf[64];

    ASSERT(fresh_tree() == 0);
    snprintf(link, sizeof link, "%s/sub", g_land);
    ASSERT(symlink(g_out, link) == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "sub/new2", in, sizeof in - 1u) !=
           FL_RESULT_OK);
    ASSERT(!exists(g_out, "new2"));
    ASSERT(fl_server_shared_resolve_local_path("sub/deeper/x", 1, &dfd, leaf, sizeof leaf) ==
           FL_RESULT_ACCES);
    ASSERT(dfd == -1 && !exists(g_out, "deeper"));
    return 0;
}

static int test_contained_bad_names_and_paths_are_refused(void) {
    const uint8_t in[] = "x";
    char longp[5200], leaf[64], bigname[400];
    int dfd, tmpfd = -1;

    ASSERT(fresh_tree() == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "../escape", in, 1u) == FL_RESULT_ACCES);
    ASSERT(fl_server_shared_overwrite_local(NULL, "a/../../escape", in, 1u) == FL_RESULT_ACCES);
    ASSERT(fl_server_shared_overwrite_local(NULL, "a/..", in, 1u) == FL_RESULT_ACCES);
    ASSERT(fl_server_shared_overwrite_local(NULL, "/tmp/fl_abs_escape", in, 1u) ==
           FL_RESULT_ACCES);
    ASSERT(fl_server_shared_overwrite_local(NULL, "a\\b", in, 1u) == FL_RESULT_ACCES);
    ASSERT(fl_server_shared_overwrite_local(NULL, "", in, 1u) == FL_RESULT_INVAL);
    ASSERT(fl_server_shared_overwrite_local(NULL, "dir/", in, 1u) == FL_RESULT_INVAL);
    ASSERT(!exists(g_dir, "escape") && !exists(g_land, "a"));
    ASSERT(access("/tmp/fl_abs_escape", F_OK) != 0);
    ASSERT(fl_server_shared_resolve_local_path("../x", 0, &tmpfd, leaf, sizeof leaf) ==
           FL_RESULT_ACCES && tmpfd == -1);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(fl_fs_write_file_atomic_in(dfd, "..", in, 1u) == -1);
    ASSERT(fl_fs_write_file_atomic_in(dfd, ".", in, 1u) == -1);
    ASSERT(fl_fs_write_file_atomic_in(dfd, "", in, 1u) == -1);
    ASSERT(fl_fs_write_file_atomic_in(dfd, "a/b", in, 1u) == -1);
    ASSERT(fl_fs_write_file_atomic_in(dfd, "/abs", in, 1u) == -1);
    ASSERT(fl_fs_write_file_atomic_in(-1, "ok", in, 1u) == -1);
    memset(bigname, 'n', sizeof bigname - 1u);
    bigname[sizeof bigname - 1u] = '\0';
    errno = 0;
    ASSERT(fl_fs_write_file_atomic_in(dfd, bigname, in, 1u) == -1 && errno == ENAMETOOLONG);
    close(dfd);
    memset(longp, 'p', sizeof longp - 1u);
    longp[sizeof longp - 1u] = '\0';
    ASSERT(fl_server_shared_overwrite_local(NULL, longp, in, 1u) == FL_RESULT_INVAL);
    ASSERT(fl_fs_write_file_atomic(longp, in, 1u) == -1);
    return 0;
}

static int test_contained_normal_overwrite_still_works(void) {
    const uint8_t a[] = "a longer first body", b[] = "short";
    char buf[64];
    struct stat st;
    char path[300];

    ASSERT(fresh_tree() == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "note.txt", a, sizeof a - 1u) == FL_RESULT_OK);
    ASSERT(fl_server_shared_overwrite_local(NULL, "note.txt", b, sizeof b - 1u) == FL_RESULT_OK);
    ASSERT(slurp(g_land, "note.txt", buf, sizeof buf) == (int)sizeof b - 1 &&
           strcmp(buf, "short") == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "x/y/./deep.txt", a, sizeof a - 1u) ==
           FL_RESULT_OK);
    snprintf(path, sizeof path, "%s/x/y/deep.txt", g_land);
    ASSERT(stat(path, &st) == 0 && S_ISREG(st.st_mode));
    ASSERT(fl_server_shared_overwrite_local(NULL, "empty.bin", NULL, 0u) == FL_RESULT_OK);
    ASSERT(slurp(g_land, "empty.bin", buf, sizeof buf) == 0);
    return 0;
}

static int test_contained_equivalent_spellings_are_gated_alike(void) {
    static const char *const forms[] = {
        "server_shared/expired/x", "server_shared/./expired/x", "server_shared//expired/x",
        "./server_shared/expired/x", "server_shared/expired//x", "server_shared/./expired/./x",
        ".//server_shared///expired/x", "./server_shared/./expired", "server_shared//expired///x"};
    const uint8_t in[] = "x";
    char path[300];
    size_t i;

    ASSERT(fresh_tree() == 0);
    for (i = 0; i < sizeof forms / sizeof *forms; i++)
        ASSERT(fl_server_shared_overwrite_local(NULL, forms[i], in, 1u) == FL_RESULT_ACCES);
    snprintf(path, sizeof path, "%s/server_shared", g_land);
    ASSERT(access(path, F_OK) != 0); /* nothing was created on the way */
    ASSERT(fl_server_shared_overwrite_local(NULL, "server_shared/expired/", in, 1u) !=
           FL_RESULT_OK);
    ASSERT(fl_server_shared_overwrite_local(NULL, "server_shared/./expired/", in, 1u) !=
           FL_RESULT_OK);
    ASSERT(fl_server_shared_overwrite_local(NULL, "server_shared/ok.txt", in, 1u) ==
           FL_RESULT_OK);
    return 0;
}

static int test_plain_name_accepts_and_rejects(void) {
    char longname[200];

    ASSERT(fl_fs_name_is_plain("plan.txt"));
    ASSERT(fl_fs_name_is_plain("quarterly report.txt"));
    ASSERT(fl_fs_name_is_plain(".hidden"));
    ASSERT(fl_fs_name_is_plain("caf\xc3\xa9.txt"));
    ASSERT(!fl_fs_name_is_plain(NULL));
    ASSERT(!fl_fs_name_is_plain(""));
    ASSERT(!fl_fs_name_is_plain("."));
    ASSERT(!fl_fs_name_is_plain(".."));
    ASSERT(!fl_fs_name_is_plain("a/b"));
    ASSERT(!fl_fs_name_is_plain("/abs"));
    ASSERT(!fl_fs_name_is_plain("a\\b"));
    ASSERT(!fl_fs_name_is_plain("../x"));
    ASSERT(!fl_fs_name_is_plain("tab\there"));
    ASSERT(!fl_fs_name_is_plain("bell\a"));
    ASSERT(!fl_fs_name_is_plain("del\x7f"));
    memset(longname, 'a', sizeof longname);
    longname[127] = '\0';
    ASSERT(fl_fs_name_is_plain(longname));
    longname[127] = 'a';
    longname[128] = '\0';
    ASSERT(!fl_fs_name_is_plain(longname));
    return 0;
}

/* Wire-offer names with separators or dot components are refused before any file is made. */
static int test_save_offer_refuses_unsafe_names(void) {
    static const char *const bad[] = {"../outside.txt", "sub/inner.txt", "..", ".", "a\\b",
                                      "ctl\x01name", "/tmp/abs.txt", "x.meta", "x.part", "x.kqtf",
                                      "x.kqseen", "x.kqbak", "x.META", "x.Part", "x.KQSEEN"};
    char base[] = "/tmp/kq_name_XXXXXX";
    char old[512];
    char path[512];
    const uint8_t payload[] = "x";
    fl_server_file_offer_t o;

    ASSERT(getcwd(old, sizeof old));
    ASSERT(mkdtemp(base));
    ASSERT(chdir(base) == 0);
    ASSERT(mkdir("cwd", 0700) == 0);
    ASSERT(chdir("cwd") == 0);
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memset(&o, 0, sizeof o);
        strncpy(o.share_id, "share-n", sizeof o.share_id - 1u);
        strncpy(o.file_name, bad[i], sizeof o.file_name - 1u);
        ASSERT(fl_server_shared_save_offer(&o, payload, 1u, path, sizeof path) == FL_RESULT_INVAL);
    }
    ASSERT(access("../outside.txt", F_OK) != 0);
    ASSERT(access("../outside.txt.meta", F_OK) != 0);
    ASSERT(access("server_shared/sub", F_OK) != 0);
    ASSERT(access("server_shared/. (2)", F_OK) != 0);
    memset(&o, 0, sizeof o);
    strncpy(o.share_id, "share-n", sizeof o.share_id - 1u);
    strncpy(o.file_name, "ok name.txt", sizeof o.file_name - 1u);
    ASSERT(fl_server_shared_save_offer(&o, payload, 1u, path, sizeof path) == FL_RESULT_OK);
    ASSERT(chdir(old) == 0);
    return 0;
}

static int test_contained_name_operations_refuse_links(void) {
    const uint8_t in[] = "data";
    char link[300], target[300];
    int dfd, fd;

    ASSERT(fresh_tree() == 0);
    ASSERT(fl_fs_contained_available() == 1);
    snprintf(target, sizeof target, "%s/victim", g_out);
    snprintf(link, sizeof link, "%s/f.part", g_land);
    ASSERT(symlink(target, link) == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    /* a link at the staging name is replaced by a new file, never written through */
    ASSERT(fl_fs_stage_file_in(dfd, "f", in, sizeof in - 1u) == 0);
    ASSERT(!exists(g_out, "victim") && exists(g_land, "f.part"));
    ASSERT(fl_fs_exists_in(dfd, "f.part") == 1 && fl_fs_exists_in(dfd, "nope") == 0);
    ASSERT(fl_fs_rename_in(dfd, "f.part", "f") == 0);
    ASSERT(fl_fs_link_in(dfd, "f", "f.kqbak") == 0 && exists(g_land, "f.kqbak"));
    ASSERT(fl_fs_unlink_in(dfd, "f.kqbak") == 0 && !exists(g_land, "f.kqbak"));
    /* a link counts as present, is not opened for reading, and is removed itself */
    snprintf(link, sizeof link, "%s/dang", g_land);
    ASSERT(symlink(target, link) == 0);
    ASSERT(fl_fs_exists_in(dfd, "dang") == 1);
    errno = 0;
    ASSERT(fl_fs_open_read_in(dfd, "dang") < 0);
    fd = fl_fs_open_read_in(dfd, "f");
    ASSERT(fd >= 0);
    close(fd);
    ASSERT(fl_fs_unlink_in(dfd, "dang") == 0 && !exists(g_out, "victim"));
    /* names with a separator or a dot name are refused */
    ASSERT(fl_fs_unlink_in(dfd, "../x") != 0 && fl_fs_rename_in(dfd, "a/b", "c") != 0);
    ASSERT(fl_fs_exists_in(dfd, "..") < 0 && fl_fs_stage_file_in(-1, "f", in, 1u) != 0);
    close(dfd);
    return 0;
}

static int test_contained_new_file_is_owner_only(void) {
    const uint8_t in[] = "x";
    char path[300];
    struct stat st;

    ASSERT(fresh_tree() == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "mode/new.txt", in, 1u) == FL_RESULT_OK);
    snprintf(path, sizeof path, "%s/mode/new.txt", g_land);
    ASSERT(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    return 0;
}

static int test_contained_overwrite_replaces_whole_file(void) {
    const uint8_t a[] = "first body", b[] = "second";
    char buf[64], path[300], other[300];
    struct stat st;

    ASSERT(fresh_tree() == 0);
    /* existing regular file is replaced */
    ASSERT(put_in(g_land, "doc.txt", "old old old old") == 0);
    ASSERT(chmod_in_land("doc.txt", 0644) == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "doc.txt", b, sizeof b - 1u) == FL_RESULT_OK);
    ASSERT(slurp(g_land, "doc.txt", buf, sizeof buf) == (int)sizeof b - 1 &&
           strcmp(buf, "second") == 0);
    ASSERT(!exists(g_land, "doc.txt.part"));
    snprintf(path, sizeof path, "%s/doc.txt", g_land);
    ASSERT(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    /* a missing leaf is created owner-only */
    ASSERT(fl_server_shared_overwrite_local(NULL, "fresh.txt", a, sizeof a - 1u) == FL_RESULT_OK);
    snprintf(path, sizeof path, "%s/fresh.txt", g_land);
    ASSERT(stat(path, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600);
    /* a leaf with a second name is replaced; the other name keeps the old content */
    ASSERT(put_in(g_land, "two.txt", "kept content") == 0);
    snprintf(path, sizeof path, "%s/two.txt", g_land);
    snprintf(other, sizeof other, "%s/two_other.txt", g_land);
    ASSERT(link(path, other) == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "two.txt", b, sizeof b - 1u) == FL_RESULT_OK);
    ASSERT(slurp(g_land, "two.txt", buf, sizeof buf) > 0 && strcmp(buf, "second") == 0);
    ASSERT(slurp(g_land, "two_other.txt", buf, sizeof buf) > 0 &&
           strcmp(buf, "kept content") == 0);
    return 0;
}

static int test_contained_overwrite_failure_keeps_original(void) {
    const uint8_t b[] = "replacement";
    char buf[64], path[300];

    ASSERT(fresh_tree() == 0);
    ASSERT(put_in(g_land, "keep.txt", "original") == 0);
    /* the staging name is taken by a directory, so the write cannot start */
    snprintf(path, sizeof path, "%s/keep.txt.part", g_land);
    ASSERT(mkdir(path, 0700) == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "keep.txt", b, sizeof b - 1u) ==
           FL_RESULT_ERR);
    ASSERT(slurp(g_land, "keep.txt", buf, sizeof buf) > 0 && strcmp(buf, "original") == 0);
    ASSERT(rmdir(path) == 0);
    /* a link leaf and a directory leaf are refused and left alone */
    snprintf(path, sizeof path, "%s/outside/keep", g_dir);
    ASSERT(put_in(g_out, "keep", "outside") == 0);
    {
        char lk[300];

        snprintf(lk, sizeof lk, "%s/lnk", g_land);
        ASSERT(symlink(path, lk) == 0);
        ASSERT(fl_server_shared_overwrite_local(NULL, "lnk", b, sizeof b - 1u) ==
               FL_RESULT_ACCES);
        ASSERT(slurp(g_out, "keep", buf, sizeof buf) > 0 && strcmp(buf, "outside") == 0);
    }
    snprintf(path, sizeof path, "%s/adir", g_land);
    ASSERT(mkdir(path, 0700) == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "adir", b, sizeof b - 1u) == FL_RESULT_ACCES);
    ASSERT(!exists(g_land, "lnk.part") && !exists(g_land, "adir.part"));
    return 0;
}

/* The unwritable-leaf checks; they need a caller that is not root (W_OK always succeeds there). */
static int unwritable_leaf_checks(void) {
    const uint8_t b[] = "replacement";
    char buf[64];

    ASSERT(put_in(g_land, "ro.txt", "read only") == 0);
    ASSERT(chmod_in_land("ro.txt", 0400) == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "ro.txt", b, sizeof b - 1u) == FL_RESULT_ACCES);
    ASSERT(slurp(g_land, "ro.txt", buf, sizeof buf) > 0 && strcmp(buf, "read only") == 0);
    ASSERT(!exists(g_land, "ro.txt.part"));
    return 0;
}

/*
 * As root the checks run in a child that first hands the landing directory to an unprivileged
 * user and drops to it, so the case is exercised and never skipped. A child that cannot drop
 * fails the test.
 */
static int run_unwritable_leaf_checks(void) {
    pid_t pid;
    int status = 0;

    if (geteuid() != 0)
        return unwritable_leaf_checks();
    ASSERT(chmod(g_dir, 0711) == 0 && chown(g_land, 65534, 65534) == 0);
    pid = fork();
    ASSERT(pid >= 0);
    if (pid == 0) {
        if (setgroups(0, NULL) != 0 || setregid(65534, 65534) != 0 ||
            setreuid(65534, 65534) != 0 || geteuid() == 0)
            _exit(2);
        _exit(unwritable_leaf_checks() == 0 ? 0 : 1);
    }
    ASSERT(waitpid(pid, &status, 0) == pid);
    ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

static int test_contained_overwrite_refuses_unwritable_and_fifo_leaf(void) {
    const uint8_t b[] = "replacement";
    char path[300];
    struct stat st;

    ASSERT(fresh_tree() == 0);
    ASSERT(run_unwritable_leaf_checks() == 0);
    /* a named pipe is refused from its status alone; it is never opened, so this cannot block */
    snprintf(path, sizeof path, "%s/pipe", g_land);
    ASSERT(mkfifo(path, 0600) == 0);
    ASSERT(fl_server_shared_overwrite_local(NULL, "pipe", b, sizeof b - 1u) == FL_RESULT_ACCES);
    ASSERT(lstat(path, &st) == 0 && S_ISFIFO(st.st_mode));
    ASSERT(!exists(g_land, "pipe.part"));
    return 0;
}

static int test_contained_destination_names_follow_the_landing_rules(void) {
    static const char *const bad[] = {"x.kqseen", "x.kqtf", "x.kqbak", "x.part", "x.meta",
                                      "sub/x.KQSEEN", "server_shared/doc.txt.Meta", "./y.PART",
                                      "ctl\x01name"};
    const uint8_t in[] = "x";
    char longname[200];
    size_t i;

    ASSERT(fresh_tree() == 0);
    for (i = 0; i < sizeof bad / sizeof *bad; i++)
        ASSERT(fl_server_shared_overwrite_local(NULL, bad[i], in, 1u) == FL_RESULT_ACCES);
    memset(longname, 'n', 150u);
    longname[150] = '\0';
    ASSERT(fl_server_shared_overwrite_local(NULL, longname, in, 1u) == FL_RESULT_ACCES);
    ASSERT(!exists(g_land, "x.kqseen") && !exists(g_land, "sub") && !exists(g_land, "server_shared"));
    ASSERT(fl_server_shared_overwrite_local(NULL, "sub/x.kqseen.txt", in, 1u) == FL_RESULT_OK);
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
    rc |= test_contained_normal_write_and_replace();
    rc |= test_contained_planted_part_link_is_not_written_through();
    rc |= test_contained_link_at_destination_is_not_followed();
    rc |= test_contained_read_refuses_link();
    rc |= test_contained_linked_subdirectory_is_refused();
    rc |= test_contained_bad_names_and_paths_are_refused();
    rc |= test_contained_normal_overwrite_still_works();
    rc |= test_contained_equivalent_spellings_are_gated_alike();
    rc |= test_contained_name_operations_refuse_links();
    rc |= test_contained_new_file_is_owner_only();
    rc |= test_contained_overwrite_replaces_whole_file();
    rc |= test_contained_overwrite_failure_keeps_original();
    rc |= test_contained_overwrite_refuses_unwritable_and_fifo_leaf();
    rc |= test_contained_destination_names_follow_the_landing_rules();
    rc |= test_plain_name_accepts_and_rejects();
    rc |= test_save_offer_refuses_unsafe_names();
    for (i = 0; i < sizeof names / sizeof *names; i++) {
        char path[128];

        path_in(path, sizeof path, names[i]);
        unlink(path);
    }
    if (g_land[0]) {
        nftw(g_land, rm_cb, 8, FTW_DEPTH | FTW_PHYS);
        nftw(g_out, rm_cb, 8, FTW_DEPTH | FTW_PHYS);
    }
    rmdir(g_dir);
    if (rc == 0)
        printf("test_fs_helpers: all tests passed\n");
    return rc;
}
