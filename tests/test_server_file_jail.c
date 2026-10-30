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
#include "cmd_server_file.h"
#include "common.h"
#include "fs_jail.h"
#include "net_client.h"
#include "session.h"

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

/* Run argv through cmd_server_file_run and return what it printed (stdout and stderr). */
static int run_captured(char **argv, int argc, char *out, size_t cap)
{
    char tmpl[] = "/tmp/sfj_out_XXXXXX";
    int fd = mkstemp(tmpl);
    int saved_out, saved_err, rc;
    cmd_server_ctx_t ctx = make_ctx();
    ssize_t n;

    if (fd < 0)
        return -1;
    fflush(stdout);
    fflush(stderr);
    saved_out = dup(STDOUT_FILENO);
    saved_err = dup(STDERR_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    rc = cmd_server_file_run(&ctx, argc, argv);

    fflush(stdout);
    fflush(stderr);
    dup2(saved_out, STDOUT_FILENO);
    dup2(saved_err, STDERR_FILENO);
    close(saved_out);
    close(saved_err);

    lseek(fd, 0, SEEK_SET);
    n = read(fd, out, cap - 1u);
    out[n > 0 ? n : 0] = '\0';
    close(fd);
    unlink(tmpl);
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
    unlink(db_path);
    return 0;
}
