/**
 * test_fs_helpers.c - the shared file helpers in kernel/core/vfs/server_shared_fs.c
 * (fl_fs_read_file, the contained calls, the local path walk) and the header-only
 * fl_fs_basename (fl/path.h).
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _GNU_SOURCE /* RTLD_NEXT, for the two call wrappers below */
#include "common.h"
#include "fl/path.h"
#include "contract_p5_file_delivery.h"
#include "server_shared_fs.h"

#include <dlfcn.h>
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

/* ---- failure injection for fl_fs_commit_files ------------------------------ */

/*
 * This test program defines renameat and linkat, so the calls server_shared_fs.c makes reach
 * these first and pass through to the C library unless a test armed them. Nothing here is in
 * the code under test.
 *   g_fail_rename_at: fail the Nth renameat from now (1 is the next), once; 0 is off.
 *   g_no_hard_links: every linkat fails, like a file system without links.
 */
static int g_fail_rename_at;
static int g_no_hard_links;
static int g_link_calls;
static char g_fail_stat_name[64]; /* fstatat of exactly this name fails (EIO); "" is off */
static int g_link_refused; /* calls that did not make a link, whatever the reason */

int renameat(int ofd, const char *from, int nfd, const char *to) {
    static int (*real)(int, const char *, int, const char *);

    if (!real)
        real = (int (*)(int, const char *, int, const char *))dlsym(RTLD_NEXT, "renameat");
    if (g_fail_rename_at > 0 && --g_fail_rename_at == 0) {
        errno = EIO;
        return -1;
    }
    return real(ofd, from, nfd, to);
}

int fstatat(int dfd, const char *path, struct stat *st, int flags) {
    static int (*real)(int, const char *, struct stat *, int);

    if (!real)
        real = (int (*)(int, const char *, struct stat *, int))dlsym(RTLD_NEXT, "fstatat");
    if (g_fail_stat_name[0] && strcmp(path, g_fail_stat_name) == 0) {
        errno = EIO;
        return -1;
    }
    return real(dfd, path, st, flags);
}

int linkat(int ofd, const char *from, int nfd, const char *to, int flags) {
    static int (*real)(int, const char *, int, const char *, int);

    if (!real)
        real = (int (*)(int, const char *, int, const char *, int))dlsym(RTLD_NEXT, "linkat");
    g_link_calls++;
    if (g_no_hard_links) {
        g_link_refused++;
        errno = EPERM;
        return -1;
    }
    if (real(ofd, from, nfd, to, flags) != 0) {
        g_link_refused++;
        return -1;
    }
    return 0;
}

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
    g_fail_rename_at = 0; /* a test that failed early must not leave an injection armed */
    g_no_hard_links = 0;
    g_fail_stat_name[0] = '\0';
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

/* ---- fl_fs_commit_files ----------------------------------------------------- */

static int holds(const char *name, const char *text) {
    char buf[512];

    return slurp(g_land, name, buf, sizeof buf) == (int)strlen(text) && strcmp(buf, text) == 0;
}

/* No staging file and no backup is left beside name. */
static int tidy(const char *name) {
    char sib[300];

    snprintf(sib, sizeof sib, "%s%s", name, FL_FS_PART_SUFFIX);
    if (exists(g_land, sib))
        return 0;
    snprintf(sib, sizeof sib, "%s%s", name, FL_FS_BACKUP_SUFFIX);
    return !exists(g_land, sib);
}

static int test_commit_files_installs_one_and_three(void) {
    const fl_fs_file_out_t one[] = {{"solo", (const uint8_t *)"alone", 5u}};
    const fl_fs_file_out_t three[] = {{"a", (const uint8_t *)"new-a", 5u},
                                      {"b", (const uint8_t *)"new-b", 5u},
                                      {"c", (const uint8_t *)"new-c", 5u}};
    char path[300];
    struct stat st;
    int dfd;

    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(fl_fs_commit_files(dfd, NULL, one, 1u) == 0);
    ASSERT(holds("solo", "alone") && tidy("solo"));
    snprintf(path, sizeof path, "%s/solo", g_land);
    ASSERT(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    /* a replaced file, a new one, and an empty body */
    ASSERT(put_in(g_land, "a", "old-a") == 0 && put_in(g_land, "c", "old-c") == 0);
    ASSERT(fl_fs_commit_files(dfd, NULL, three, 3u) == 0);
    ASSERT(holds("a", "new-a") && holds("b", "new-b") && holds("c", "new-c"));
    ASSERT(tidy("a") && tidy("b") && tidy("c"));
    {
        const fl_fs_file_out_t empty[] = {{"a", NULL, 0u}};

        ASSERT(fl_fs_commit_files(dfd, NULL, empty, 1u) == 0 && holds("a", ""));
    }
    close(dfd);
    return 0;
}

static int test_commit_files_failed_rename_puts_everything_back(void) {
    const fl_fs_file_out_t files[] = {{"a", (const uint8_t *)"new-a", 5u},
                                      {"fresh", (const uint8_t *)"new-fresh", 9u},
                                      {"b", (const uint8_t *)"new-b", 5u}};
    int dfd, attempt;

    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    /* The third rename fails: a is back, fresh (new) is gone again, b was never touched. */
    for (attempt = 1; attempt <= 3; attempt++) {
        ASSERT(put_in(g_land, "a", "old-a") == 0 && put_in(g_land, "b", "old-b") == 0);
        g_fail_rename_at = attempt;
        errno = 0;
        ASSERT(fl_fs_commit_files(dfd, NULL, files, 3u) == -1 && errno == EIO);
        g_fail_rename_at = 0;
        ASSERT(holds("a", "old-a") && holds("b", "old-b") && !exists(g_land, "fresh"));
        ASSERT(tidy("a") && tidy("fresh") && tidy("b"));
    }
    /* once the failure is gone the same call goes through */
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 3u) == 0);
    ASSERT(holds("a", "new-a") && holds("fresh", "new-fresh") && holds("b", "new-b"));
    close(dfd);
    return 0;
}

/* An existence check that fails is not "absent": nothing is replaced and the parts go. */
static int test_commit_files_unknown_existence_fails_closed(void) {
    const fl_fs_file_out_t files[] = {{"a", (const uint8_t *)"new-a", 5u},
                                      {"b", (const uint8_t *)"new-b", 5u},
                                      {"c", (const uint8_t *)"new-c", 5u}};
    int dfd;

    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(put_in(g_land, "a", "old-a") == 0 && put_in(g_land, "b", "old-b") == 0);
    snprintf(g_fail_stat_name, sizeof g_fail_stat_name, "b");
    errno = 0;
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 3u) == -1 && errno == EIO);
    g_fail_stat_name[0] = '\0';
    ASSERT(holds("a", "old-a") && holds("b", "old-b") && !exists(g_land, "c"));
    ASSERT(tidy("a") && tidy("b") && tidy("c"));
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 3u) == 0 && holds("b", "new-b"));
    close(dfd);
    return 0;
}

static int test_commit_files_copy_fallback_where_links_are_missing(void) {
    const fl_fs_file_out_t files[] = {{"a", (const uint8_t *)"new-a", 5u},
                                      {"b", (const uint8_t *)"new-b", 5u}};
    int dfd;

    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    g_no_hard_links = 1;
    g_link_calls = 0;
    ASSERT(put_in(g_land, "a", "old-a") == 0 && put_in(g_land, "b", "old-b") == 0);
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 2u) == 0);
    ASSERT(g_link_calls == 2 && holds("a", "new-a") && holds("b", "new-b"));
    ASSERT(tidy("a") && tidy("b"));
    /* a failure after the first rename restores the first file from its copied backup */
    ASSERT(put_in(g_land, "a", "old-a") == 0 && put_in(g_land, "b", "old-b") == 0);
    g_fail_rename_at = 2;
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 2u) == -1);
    g_fail_rename_at = 0;
    g_no_hard_links = 0;
    ASSERT(holds("a", "old-a") && holds("b", "old-b") && tidy("a") && tidy("b"));
    close(dfd);
    return 0;
}

/* The longest name works and a longer one is refused untouched, with links and without. */
static int name_length_checks(void) {
    char longest[FL_FS_NAME_MAX + 8u], too_long[FL_FS_NAME_MAX + 8u];
    fl_fs_file_out_t f;
    int dfd;

    memset(longest, 'n', FL_FS_COMMIT_NAME_MAX);
    longest[FL_FS_COMMIT_NAME_MAX] = '\0';
    memset(too_long, 'n', FL_FS_COMMIT_NAME_MAX + 1u);
    too_long[FL_FS_COMMIT_NAME_MAX + 1u] = '\0';
    ASSERT(strlen(longest) == 244u && strlen(too_long) == 245u);
    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    f = (fl_fs_file_out_t){too_long, (const uint8_t *)"x", 1u};
    errno = 0;
    ASSERT(fl_fs_commit_files(dfd, NULL, &f, 1u) == -1 && errno == ENAMETOOLONG);
    ASSERT(!exists(g_land, too_long) && tidy(too_long));
    /* created, then replaced with a backup of the old one */
    f = (fl_fs_file_out_t){longest, (const uint8_t *)"x", 1u};
    ASSERT(fl_fs_commit_files(dfd, NULL, &f, 1u) == 0 && holds(longest, "x") && tidy(longest));
    f = (fl_fs_file_out_t){longest, (const uint8_t *)"yz", 2u};
    ASSERT(fl_fs_commit_files(dfd, NULL, &f, 1u) == 0 && holds(longest, "yz") && tidy(longest));
    close(dfd);
    return 0;
}

static int test_commit_files_name_length_leaves_room_for_every_sibling(void) {
    ASSERT(FL_FS_COMMIT_NAME_MAX == 244u);
    ASSERT(name_length_checks() == 0);
    /* the copied backup is staged as <name>.kqbak.part, which the same limit covers */
    g_no_hard_links = 1;
    ASSERT(name_length_checks() == 0);
    g_no_hard_links = 0;
    return 0;
}

/* A staging name that cannot be made fails the call and removes the parts made before it. */
static int test_commit_files_failed_staging_leaves_nothing(void) {
    const fl_fs_file_out_t files[] = {{"a", (const uint8_t *)"new-a", 5u},
                                      {"b", (const uint8_t *)"new-b", 5u},
                                      {"c", (const uint8_t *)"new-c", 5u}};
    char block[300];
    int dfd, stage;

    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(put_in(g_land, "a", "old-a") == 0);
    for (stage = 1; stage <= 2; stage++) {
        /* a directory at the second, then at the third, staging name */
        snprintf(block, sizeof block, "%s/%s.part", g_land, stage == 1 ? "b" : "c");
        ASSERT(mkdir(block, 0700) == 0);
        ASSERT(fl_fs_commit_files(dfd, NULL, files, 3u) == -1);
        ASSERT(!exists(g_land, "a.part"));
        ASSERT(stage == 2 || !exists(g_land, "c.part")); /* the blocked name is ours */
        ASSERT(stage == 1 || !exists(g_land, "b.part"));
        ASSERT(holds("a", "old-a") && !exists(g_land, "b") && !exists(g_land, "c"));
        ASSERT(!exists(g_land, "a.kqbak"));
        ASSERT(rmdir(block) == 0);
    }
    close(dfd);
    return 0;
}

static int test_commit_files_drops_a_stale_backup(void) {
    const fl_fs_file_out_t files[] = {{"a", (const uint8_t *)"new-a", 5u},
                                      {"b", (const uint8_t *)"new-b", 5u}};
    int dfd;

    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(put_in(g_land, "a", "old-a") == 0 && put_in(g_land, "a.kqbak", "stale") == 0);
    ASSERT(put_in(g_land, "b", "old-b") == 0 && put_in(g_land, "b.kqbak", "stale") == 0);
    g_link_calls = g_link_refused = 0;
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 2u) == 0);
    /* the leftover was removed first, so each backup was a plain hard link */
    ASSERT(g_link_calls == 2 && g_link_refused == 0);
    ASSERT(holds("a", "new-a") && holds("b", "new-b") && tidy("a") && tidy("b"));
    /* a failed run restores the real original, never the leftover */
    ASSERT(put_in(g_land, "a", "old-a") == 0 && put_in(g_land, "a.kqbak", "stale") == 0);
    g_fail_rename_at = 2;
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 2u) == -1);
    g_fail_rename_at = 0;
    ASSERT(holds("a", "old-a") && tidy("a") && tidy("b"));
    /* a leftover staging name is replaced as well */
    ASSERT(put_in(g_land, "a.part", "half") == 0);
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 2u) == 0 && holds("a", "new-a") && tidy("a"));
    close(dfd);
    return 0;
}

static int test_commit_files_refuses_bad_calls_before_writing(void) {
    const fl_fs_file_out_t five[] = {{"f1", (const uint8_t *)"1", 1u},
                                     {"f2", (const uint8_t *)"2", 1u},
                                     {"f3", (const uint8_t *)"3", 1u},
                                     {"f4", (const uint8_t *)"4", 1u},
                                     {"f5", (const uint8_t *)"5", 1u}};
    const fl_fs_file_out_t dup[] = {{"d", (const uint8_t *)"1", 1u},
                                    {"d", (const uint8_t *)"2", 1u}};
    const fl_fs_file_out_t clash[] = {{"s", (const uint8_t *)"1", 1u},
                                      {"s.part", (const uint8_t *)"2", 1u}};
    const fl_fs_file_out_t sep[] = {{"sub/x", (const uint8_t *)"1", 1u}};
    const fl_fs_file_out_t dots[] = {{"..", (const uint8_t *)"1", 1u}};
    const fl_fs_file_out_t nodata[] = {{"nd", NULL, 3u}};
    const fl_fs_file_out_t good[] = {{"g", (const uint8_t *)"1", 1u}};
    char listing[300];
    int dfd, i;

    ASSERT(fresh_tree() == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(FL_FS_COMMIT_FILES_MAX == 4u);
    ASSERT(fl_fs_commit_files(dfd, NULL, five, 4u) == 0); /* the cap itself is fine */
    for (i = 1; i <= 4; i++) {
        snprintf(listing, sizeof listing, "f%d", i);
        ASSERT(exists(g_land, listing));
    }
    errno = 0;
    ASSERT(fl_fs_commit_files(dfd, NULL, five, 5u) == -1 && errno == EINVAL);
    ASSERT(!exists(g_land, "f5") && !exists(g_land, "f5.part") && tidy("f1"));
    ASSERT(fl_fs_commit_files(dfd, NULL, good, 0u) == -1);
    ASSERT(fl_fs_commit_files(dfd, NULL, NULL, 1u) == -1);
    ASSERT(fl_fs_commit_files(dfd, NULL, dup, 2u) == -1 && !exists(g_land, "d"));
    ASSERT(fl_fs_commit_files(dfd, NULL, clash, 2u) == -1 && !exists(g_land, "s"));
    ASSERT(fl_fs_commit_files(dfd, NULL, sep, 1u) == -1 && !exists(g_land, "sub"));
    ASSERT(fl_fs_commit_files(dfd, NULL, dots, 1u) == -1);
    ASSERT(fl_fs_commit_files(dfd, NULL, nodata, 1u) == -1 && !exists(g_land, "nd"));
    /* the path is not a silent fallback where the contained calls exist; a closed fd fails */
    errno = 0;
    ASSERT(fl_fs_commit_files(-1, g_land, good, 1u) == -1 && errno == EBADF);
    ASSERT(!exists(g_land, "g"));
    close(dfd);
    ASSERT(fl_fs_commit_files(dfd, NULL, good, 1u) == -1 && !exists(g_land, "g"));
    return 0;
}

static int test_commit_files_replaces_a_link_instead_of_following_it(void) {
    const fl_fs_file_out_t files[] = {{"lnk", (const uint8_t *)"new", 3u}};
    char target[300], link_path[300];
    struct stat st;
    int dfd;

    ASSERT(fresh_tree() == 0);
    ASSERT(put_in(g_out, "elsewhere", "untouched") == 0);
    snprintf(target, sizeof target, "%s/elsewhere", g_out);
    snprintf(link_path, sizeof link_path, "%s/lnk", g_land);
    ASSERT(symlink(target, link_path) == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(fl_fs_commit_files(dfd, NULL, files, 1u) == 0);
    ASSERT(lstat(link_path, &st) == 0 && S_ISREG(st.st_mode) && holds("lnk", "new"));
    {
        char buf[32];

        ASSERT(slurp(g_out, "elsewhere", buf, sizeof buf) > 0 &&
               strcmp(buf, "untouched") == 0);
    }
    close(dfd);
    return 0;
}

static int test_landing_directory_helpers(void) {
    char out[64], tiny[4];
    uint8_t *data = NULL;
    size_t len = 0;
    int dfd;

    ASSERT(fresh_tree() == 0);
    ASSERT(put_in(g_land, "here", "bytes") == 0);
    dfd = fl_fs_dir_open(g_land);
    ASSERT(dfd >= 0);
    ASSERT(fl_fs_exists_at(dfd, NULL, "here") == 1 && fl_fs_exists_at(dfd, NULL, "gone") == 0);
    ASSERT(fl_fs_read_file_at(dfd, NULL, "here", 64u, &data, &len) == 0 && len == 5u &&
           memcmp(data, "bytes", 5u) == 0);
    free(data);
    errno = 0;
    ASSERT(fl_fs_exists_at(-1, g_land, "here") == -1 && errno == EBADF);
    ASSERT(fl_fs_read_file_at(-1, g_land, "here", 64u, &data, &len) == -1 && data == NULL);
    close(dfd);
    ASSERT(fl_fs_join_path(out, sizeof out, "dir", "n") == 0 && strcmp(out, "dir/n") == 0);
    ASSERT(fl_fs_join_path(out, sizeof out, "dir/", "n") == 0 && strcmp(out, "dir/n") == 0);
    ASSERT(fl_fs_join_path(out, sizeof out, "", "n") == 0 && strcmp(out, "n") == 0);
    ASSERT(fl_fs_join_path(tiny, sizeof tiny, "dir", "name") != 0);
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
    rc |= test_commit_files_installs_one_and_three();
    rc |= test_commit_files_failed_rename_puts_everything_back();
    rc |= test_commit_files_unknown_existence_fails_closed();
    rc |= test_commit_files_copy_fallback_where_links_are_missing();
    rc |= test_commit_files_name_length_leaves_room_for_every_sibling();
    rc |= test_commit_files_failed_staging_leaves_nothing();
    rc |= test_commit_files_drops_a_stale_backup();
    rc |= test_commit_files_refuses_bad_calls_before_writing();
    rc |= test_commit_files_replaces_a_link_instead_of_following_it();
    rc |= test_landing_directory_helpers();
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
