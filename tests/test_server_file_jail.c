/*
 * test_server_file_jail.c - the `server file` verbs that read a local file (offer, send -t, log,
 * permit) refuse a jailed, unresolvable or quarantined path before they read anything.
 *
 * Each case runs the real cmd_server_file_run with a context that says the server is running, so
 * the verb gets as far as its path check. A denial prints one message and returns 1; a verb that
 * went on to read the file would instead print the message of the call after the check (the
 * checkpoint warning, "cannot read file", the history line), so each case also asserts that none
 * of those appear. The control case shows a path inside the jail is not refused.
 */
#include "cmd_decl.h"
#include "cmd_server_file.h"
#include "cmd_util.h"
#include "kq_storage.h"
#include "common.h"
#include "fs_jail.h"
#include "net_client.h"
#include "session.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
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

extern char g_fs_jail_root[];

/* snprintf that fails the setup instead of silently cutting a path. */
#define SNP(dst, ...) \
    do { \
        int n_ = snprintf((dst), sizeof(dst), __VA_ARGS__); \
        if (n_ < 0 || (size_t)n_ >= sizeof(dst)) { \
            fprintf(stderr, "FAIL [%s:%d]: path too long for the fixture\n", __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

/* g_cwd and g_vm_root are CWD_MAX (256) bytes: copy with an explicit end instead of snprintf. */
static void set_str(char *dst, size_t cap, const char *src)
{
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = '\0';
}

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_running = 1;
static fl_net_client_t g_client;

static cmd_server_ctx_t make_ctx(void)
{
    cmd_server_ctx_t ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.mutex = &g_mutex;
    ctx.server_running = &g_running;
    ctx.client = &g_client;
    return ctx;
}

struct capture {
    char tmpl[32];
    int fd, saved_out, saved_err;
};

static int capture_begin(struct capture *c)
{
    snprintf(c->tmpl, sizeof(c->tmpl), "/tmp/sfj_out_XXXXXX");
    c->fd = mkstemp(c->tmpl);
    if (c->fd < 0)
        return -1;
    fflush(stdout);
    fflush(stderr);
    c->saved_out = dup(STDOUT_FILENO);
    c->saved_err = dup(STDERR_FILENO);
    dup2(c->fd, STDOUT_FILENO);
    dup2(c->fd, STDERR_FILENO);
    return 0;
}

static void capture_end(struct capture *c, char *out, size_t cap)
{
    ssize_t n;

    fflush(stdout);
    fflush(stderr);
    dup2(c->saved_out, STDOUT_FILENO);
    dup2(c->saved_err, STDERR_FILENO);
    close(c->saved_out);
    close(c->saved_err);
    lseek(c->fd, 0, SEEK_SET);
    n = read(c->fd, out, cap - 1u);
    out[n > 0 ? n : 0] = '\0';
    close(c->fd);
    unlink(c->tmpl);
}

/* Run argv through cmd_server_file_run and return what it printed (stdout and stderr). */
static int run_captured(char **argv, int argc, char *out, size_t cap)
{
    struct capture c;
    cmd_server_ctx_t ctx = make_ctx();
    int rc;

    if (capture_begin(&c) != 0)
        return -1;
    rc = cmd_server_file_run(&ctx, argc, argv);
    capture_end(&c, out, cap);
    return rc;
}

static void rmrf(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;

    if (!d) {
        unlink(path);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char sub[PATH_MAX];
        struct stat st;

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
        if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode))
            rmrf(sub);
        else
            unlink(sub);
    }
    closedir(d);
    rmdir(path);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

/* What a verb prints only when it went on to use the file after the path check. */
static int went_on_to_read(const char *out)
{
    return strstr(out, "server file log:") != NULL || strstr(out, "no history for") != NULL ||
           strstr(out, "server file permit:") != NULL || strstr(out, "cannot read file") != NULL ||
           strstr(out, "offer create failed") != NULL;
}

struct fixture {
    char outer[PATH_MAX];   /* jail parent: also holds the file outside the jail */
    char outside[PATH_MAX]; /* a real file outside the jail */
    char inside[PATH_MAX];  /* a path inside the jail (does not exist) */
    char quarantined[PATH_MAX];
};

static int setup(struct fixture *fx)
{
    char tmpl[] = "/tmp/sfj_XXXXXX";
    char *canon;
    char inner[PATH_MAX];
    char dir[PATH_MAX];

    if (!mkdtemp(tmpl))
        return 1;
    canon = realpath(tmpl, NULL);
    ASSERT(canon != NULL);
    SNP(fx->outer, "%s", canon);
    free(canon);

    SNP(fx->outside, "%s/outside.txt", fx->outer);
    write_file(fx->outside, "not for the jail\n");
    SNP(dir, "%s/server_shared", fx->outer);
    mkdir(dir, 0700);
    SNP(dir, "%s/server_shared/expired", fx->outer);
    mkdir(dir, 0700);
    SNP(fx->quarantined, "%s/server_shared/expired/old.txt", fx->outer);
    write_file(fx->quarantined, "expired\n");

    g_vm_mode = 1;
    set_str(g_vm_root, sizeof(g_vm_root), fx->outer);
    g_fs_jail_root[0] = '\0';
    ASSERT(chdir(fx->outer) == 0);
    set_str(g_cwd, sizeof(g_cwd), fx->outer);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);

    SNP(inner, "%s/%s", fx->outer, FS_JAIL_HOST_SUBDIR);
    SNP(fx->inside, "%s/inside.txt", inner);
    set_str(g_cwd, sizeof(g_cwd), inner);
    ASSERT(chdir(inner) == 0);
    return 0;
}

static void teardown(struct fixture *fx)
{
    g_vm_mode = 0;
    g_vm_root[0] = '\0';
    g_fs_jail_root[0] = '\0';
    chdir("/tmp");
    rmrf(fx->outer);
}

/* Run one verb on one path; every verb is given the same chance to read the file. */
enum verb { V_OFFER, V_SEND_T, V_LOG, V_PERMIT };

static int run_verb(enum verb v, const char *path, char *out, size_t cap)
{
    char *offer[] = {"server", "file", (char *)path};
    char *send_t[] = {"server", "file", "send", "-t", (char *)path, "-user", "bob"};
    char *log[] = {"server", "file", "log", (char *)path};
    char *permit[] = {"server", "file", "permit", (char *)path};

    switch (v) {
    case V_OFFER:
        return run_captured(offer, 3, out, cap);
    case V_SEND_T:
        return run_captured(send_t, 7, out, cap);
    case V_LOG:
        return run_captured(log, 4, out, cap);
    default:
        return run_captured(permit, 4, out, cap);
    }
}

static const char *verb_name(enum verb v)
{
    static const char *names[] = {"offer", "send -t", "log", "permit"};

    return names[v];
}

static int expect_denied(enum verb v, const char *path, const char *why, const char *what)
{
    char out[4096];
    int rc = run_verb(v, path, out, sizeof(out));

    if (rc != 1 || !strstr(out, why) || went_on_to_read(out)) {
        fprintf(stderr, "FAIL: server file %s on %s: rc=%d, wanted \"%s\"\n%s\n", verb_name(v), what, rc,
                why, out);
        return 1;
    }
    return 0;
}

static int test_denials(void)
{
    struct fixture fx;
    char deep[PATH_MAX];
    int v;
    int rc = 1;

    if (setup(&fx))
        return 1;
    /* A signed-in account that is not elevated (root and sudo are deliberately outside the jail),
     * so permit and send -t reach their path check and the jail applies. */
    if (fl_session_set_user("Flinstone") != FL_RESULT_OK)
        goto done;

    /* 70 directory segments then ".." components: the resolver cannot finish it. */
    deep[0] = '\0';
    for (int i = 0; i < 70; i++)
        strcat(deep, "d/");
    strcat(deep, "../../../../../../../../../../../../etc/passwd");

    for (v = V_OFFER; v <= V_PERMIT; v++) {
        if (expect_denied((enum verb)v, fx.outside, "jail access policy", "a file outside the jail"))
            goto done;
        if (expect_denied((enum verb)v, deep, "path too long to check", "an unresolvable path"))
            goto done;
        if (expect_denied((enum verb)v, fx.quarantined, "expired server share quarantine",
                          "an expired-share quarantine path"))
            goto done;
    }
    /* The outside file is untouched. */
    {
        struct stat st;

        if (stat(fx.outside, &st) != 0 || st.st_size != (off_t)strlen("not for the jail\n"))
            goto done;
    }
    rc = 0;
done:
    teardown(&fx);
    return rc;
}

/* Control: a path inside the jail is not refused by the check (the verb goes on past it). */
static int test_inside_jail_is_not_refused(void)
{
    struct fixture fx;
    char out[4096];
    int rc = 1;

    if (setup(&fx))
        return 1;
    (void)run_verb(V_LOG, fx.inside, out, sizeof(out));
    if (strstr(out, "jail access policy") || strstr(out, "path too long") ||
        strstr(out, "quarantine")) {
        fprintf(stderr, "FAIL: a path inside the jail was refused:\n%s\n", out);
        goto done;
    }
    rc = 0;
done:
    teardown(&fx);
    return rc;
}

static int exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

/* createdisk judges and creates the same, fully resolved file. */
static int test_createdisk(void)
{
    struct fixture fx;
    char out[4096];
    char deep[PATH_MAX];
    char outside_disk[PATH_MAX];
    char inside_disk[PATH_MAX];
    char quarantined_disk[PATH_MAX];
    char *outside_arg[] = {"createdisk", NULL, "8", "8"};
    char *inside_arg[] = {"createdisk", "vol", "8", "8"};
    char *deep_arg[] = {"createdisk", NULL, "8", "8"};
    struct capture c;
    int rc = 1;

    if (setup(&fx))
        return 1;
    SNP(outside_disk, "%s/outdisk_disk.dat", fx.outer);
    SNP(quarantined_disk, "%s/server_shared/expired/qdisk_disk.dat", fx.outer);
    SNP(inside_disk, "%s/%s/vol_disk.dat", fx.outer, FS_JAIL_HOST_SUBDIR);

    /* Jail active: a volume outside the jail is refused and nothing is created. */
    {
        char name[PATH_MAX];

        SNP(name, "%s/outdisk", fx.outer);
        outside_arg[1] = name;
        if (capture_begin(&c) != 0)
            goto done;
        rc = cmd_createdisk_run(4, outside_arg);
        capture_end(&c, out, sizeof(out));
        if (rc != 1 || !strstr(out, "jail access policy") || exists(outside_disk)) {
            fprintf(stderr, "FAIL: createdisk outside the jail: rc=%d\n%s\n", rc, out);
            rc = 1;
            goto done;
        }
    }
    /* A path the resolver cannot finish is refused, with the jail on. */
    deep[0] = '\0';
    for (int i = 0; i < 70; i++)
        strcat(deep, "d/");
    strcat(deep, "../../../../../../../../../../../../vol");
    deep_arg[1] = deep;
    if (capture_begin(&c) != 0)
        goto done;
    rc = cmd_createdisk_run(4, deep_arg);
    capture_end(&c, out, sizeof(out));
    if (rc != 1 || !strstr(out, "path too long to check")) {
        fprintf(stderr, "FAIL: createdisk with an unresolvable path: rc=%d\n%s\n", rc, out);
        rc = 1;
        goto done;
    }
    /* Control: a volume inside the jail is created, with the file named from the checked path. */
    if (capture_begin(&c) != 0)
        goto done;
    rc = cmd_createdisk_run(4, inside_arg);
    capture_end(&c, out, sizeof(out));
    if (rc != 0 || !exists(inside_disk)) {
        fprintf(stderr, "FAIL: createdisk inside the jail: rc=%d\n%s\n", rc, out);
        rc = 1;
        goto done;
    }

    /* Jail off: the quarantine still applies, and an unresolvable path is still refused. */
    g_vm_mode = 0;
    g_fs_jail_root[0] = '\0';
    {
        char name[PATH_MAX];

        SNP(name, "%s/server_shared/expired/qdisk", fx.outer);
        outside_arg[1] = name;
        if (capture_begin(&c) != 0)
            goto done;
        rc = cmd_createdisk_run(4, outside_arg);
        capture_end(&c, out, sizeof(out));
        if (rc != 1 || !strstr(out, "expired server share quarantine") || exists(quarantined_disk)) {
            fprintf(stderr, "FAIL: createdisk into the quarantine: rc=%d\n%s\n", rc, out);
            rc = 1;
            goto done;
        }
    }
    if (capture_begin(&c) != 0)
        goto done;
    rc = cmd_createdisk_run(4, deep_arg);
    capture_end(&c, out, sizeof(out));
    if (rc != 1 || !strstr(out, "path too long to check")) {
        fprintf(stderr, "FAIL: createdisk with an unresolvable path and no jail: rc=%d\n%s\n", rc, out);
        rc = 1;
        goto done;
    }
    rc = 0;
done:
    teardown(&fx);
    return rc;
}

/* cmd_jail_blocked_path fails closed on its own: with a jail active it refuses an input the
 * resolver cannot finish, even though the cut-down path it is also given looks harmless. */
static int test_jail_check_fails_closed(void)
{
    struct fixture fx;
    char deep[PATH_MAX];
    char cut[CWD_MAX];
    char out[1024];
    struct capture c;
    int blocked, rc = 1;

    if (setup(&fx))
        return 1;
    deep[0] = '\0';
    for (int i = 0; i < 70; i++)
        strcat(deep, "d/");
    strcat(deep, "../../../../../../../../../../../../x");
    resolve_path(deep, cut, sizeof(cut));
    if (capture_begin(&c) != 0)
        goto done;
    blocked = cmd_jail_blocked_path("probe", deep, cut);
    capture_end(&c, out, sizeof(out));
    if (blocked != 1 || !strstr(out, "path too long to check")) {
        fprintf(stderr, "FAIL: cmd_jail_blocked_path let an unresolvable input through: %d\n%s\n", blocked,
                out);
        goto done;
    }
    /* And it still lets an ordinary path inside the jail through. */
    if (capture_begin(&c) != 0)
        goto done;
    blocked = cmd_jail_blocked_path("probe", "inside.txt", fx.inside);
    capture_end(&c, out, sizeof(out));
    if (blocked != 0) {
        fprintf(stderr, "FAIL: cmd_jail_blocked_path refused a path inside the jail:\n%s\n", out);
        goto done;
    }
    rc = 0;
done:
    teardown(&fx);
    return rc;
}

/* The storage table records every path the checkpoint code is asked to read. */
static char g_seen[16][PATH_MAX];
static int g_nseen;
static fl_kq_storage_t g_rec;

static void note_path(const char *path)
{
    if (g_nseen < 16) {
        strncpy(g_seen[g_nseen], path, PATH_MAX - 1u);
        g_seen[g_nseen][PATH_MAX - 1u] = '\0';
        g_nseen++;
    }
}

static int rec_read(void *ctx, const char *path, size_t max, uint8_t **data, size_t *len)
{
    note_path(path);
    return fl_kq_storage_default()->read_file(ctx, path, max, data, len);
}

static int rec_exists(void *ctx, const char *path)
{
    note_path(path);
    return fl_kq_storage_default()->exists(ctx, path);
}

/* Every path the checkpoint layer was handed is the resolved one (absolute, under `want`), never
 * the relative text that was typed. */
static int seen_only_resolved(const char *want, const char *what)
{
    if (g_nseen == 0) {
        fprintf(stderr, "FAIL: %s read nothing; the test cannot tell which path it used\n", what);
        return 1;
    }
    for (int i = 0; i < g_nseen; i++) {
        if (g_seen[i][0] != '/' || strncmp(g_seen[i], want, strlen(want)) != 0) {
            fprintf(stderr, "FAIL: %s read \"%s\", not the checked path \"%s\"\n", what, g_seen[i], want);
            return 1;
        }
    }
    return 0;
}

/* The shell's working directory (g_cwd) is not the process's: a relative name means different
 * files to each. The verbs must act on the one they checked. */
static int test_verbs_use_the_checked_path(void)
{
    struct fixture fx;
    char sub[PATH_MAX];
    char doc[PATH_MAX];
    char out[4096];
    char *log[] = {"server", "file", "log", "doc.txt"};
    char *permit[] = {"server", "file", "permit", "doc.txt"};
    char *offer[] = {"server", "file", "doc.txt"};
    const fl_kq_storage_t *was = NULL;
    int rc = 1;

    if (setup(&fx))
        return 1;
    SNP(sub, "%s/%s/sub", fx.outer, FS_JAIL_HOST_SUBDIR);
    SNP(doc, "%s/doc.txt", sub);
    mkdir(sub, 0700);
    write_file(doc, "text\n");
    set_str(g_cwd, sizeof(g_cwd), sub); /* the process stays in the jail root, one level up */
    if (fl_session_set_user("Flinstone") != FL_RESULT_OK)
        goto done;

    g_rec = *fl_kq_storage_default();
    g_rec.read_file = rec_read;
    g_rec.exists = rec_exists;
    was = fl_kq_cp_set_storage(&g_rec);

    g_nseen = 0;
    (void)run_captured(log, 4, out, sizeof(out));
    if (seen_only_resolved(doc, "server file log"))
        goto done;

    g_nseen = 0;
    (void)run_captured(permit, 4, out, sizeof(out));
    if (seen_only_resolved(doc, "server file permit"))
        goto done;

    /* The offer stats the file before anything else: it exists only at the checked path. */
    (void)run_captured(offer, 3, out, sizeof(out));
    if (strstr(out, "cannot read file")) {
        fprintf(stderr, "FAIL: server file looked for the typed name, not the checked path:\n%s\n", out);
        goto done;
    }
    rc = 0;
done:
    if (was)
        (void)fl_kq_cp_set_storage(was);
    teardown(&fx);
    return rc;
}

/* `write` joins its words into a bounded buffer. */
static int test_write_joins_and_bounds_its_words(void)
{
    struct fixture fx;
    char path[PATH_MAX];
    char text[8192];
    char *args[700];
    char word[] = "0123456789";
    char *small[] = {"write", "w.txt", "aaa", "bbb", "ccc"};
    FILE *f;
    size_t n;
    int argc = 0, rc = 1;

    if (setup(&fx))
        return 1;
    g_vm_mode = 0; /* no jail: this is about the copy */
    g_fs_jail_root[0] = '\0';
    SNP(path, "%s/w.txt", fx.outer);
    set_str(g_cwd, sizeof(g_cwd), fx.outer);

    if (cmd_write_run(5, small) != 0)
        goto done;
    f = fopen(path, "r");
    if (!f)
        goto done;
    n = fread(text, 1, sizeof(text) - 1u, f);
    fclose(f);
    text[n] = '\0';
    if (strcmp(text, "aaa bbb ccc") != 0) {
        fprintf(stderr, "FAIL: write joined \"%s\"\n", text);
        goto done;
    }

    /* Far more words than fit: it stops before the buffer end and keeps only whole words. */
    args[argc++] = "write";
    args[argc++] = "w.txt";
    for (int i = 0; i < 600; i++)
        args[argc++] = word;
    if (cmd_write_run(argc, args) != 0)
        goto done;
    f = fopen(path, "r");
    if (!f)
        goto done;
    n = fread(text, 1, sizeof(text) - 1u, f);
    fclose(f);
    text[n] = '\0';
    if (n >= 4096u || n < 4000u || (n + 1u) % 11u != 0u) {
        fprintf(stderr, "FAIL: write kept %zu bytes of an oversized argument list\n", n);
        goto done;
    }
    rc = 0;
done:
    teardown(&fx);
    return rc;
}

int main(void)
{
    char db_path[] = "/tmp/sfj_users_XXXXXX";
    int db_fd = mkstemp(db_path);

    /* A private user database with the lab defaults, so the session can sign in as root. */
    if (db_fd < 0)
        return 1;
    close(db_fd);
    unlink(db_path);
    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
    (void)setenv("FL_USERS_DB_PATH", db_path, 1);
    if (test_denials() != 0)
        return 1;
    printf("test_server_file_jail: denials ... ok\n");
    if (test_inside_jail_is_not_refused() != 0)
        return 1;
    printf("test_server_file_jail: inside the jail is not refused ... ok\n");
    if (test_createdisk() != 0)
        return 1;
    printf("test_server_file_jail: createdisk checks the file it creates ... ok\n");
    if (test_jail_check_fails_closed() != 0)
        return 1;
    printf("test_server_file_jail: the jail check fails closed on its own ... ok\n");
    if (test_verbs_use_the_checked_path() != 0)
        return 1;
    printf("test_server_file_jail: log, permit and offer use the checked path ... ok\n");
    if (test_write_joins_and_bounds_its_words() != 0)
        return 1;
    printf("test_server_file_jail: write joins and bounds its words ... ok\n");
    unlink(db_path);
    return 0;
}
