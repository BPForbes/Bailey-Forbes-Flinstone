/*
 * test_fs_provider_bytes.c - fs_provider_t read_bytes / write_bytes / write_atomic and the
 * fm_read_bytes / fm_write_bytes / fm_write_atomic facade calls (issue 384, D14).
 * Only temp directories under /tmp; file contents are throwaway test bytes, no secrets.
 */
#include "fs_facade.h"
#include "fs_jail.h"
#include "fs_policy.h"
#include "fs_provider.h"
#include "path_log.h"
#include "common.h"
#include "server_shared_fs.h"
#include <dirent.h>
#include "mem_domain.h"
#include <errno.h>
#include <limits.h>
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

/* Embedded NUL, high bytes and a CR/LF pair: read_text/write_text would mangle this. */
static const uint8_t k_blob[] = { 'a', 0x00, 'b', 0xff, 0x80, 0x00, 0x00, '\r', '\n', 'z' };

static char g_dir[PATH_MAX];

static int fresh_dir(void) {
    snprintf(g_dir, sizeof g_dir, "/tmp/fs_prov_bytes_XXXXXX");
    return mkdtemp(g_dir) ? 0 : -1;
}

static void path_of(char *out, size_t cap, const char *name) {
    snprintf(out, cap, "%s/%s", g_dir, name);
}

/* path + ".part" without a truncated snprintf into a PATH_MAX buffer. */
static int part_path(char *out, size_t cap, const char *path) {
    size_t n;
    if (!out || !path || cap < 6)
        return -1;
    n = strlen(path);
    if (n + 5 >= cap)
        return -1;
    memcpy(out, path, n);
    memcpy(out + n, ".part", 6);
    return 0;
}

static void rmrf(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            char sub[PATH_MAX];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
            rmrf(sub);
        }
        if (d) closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

static int exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

static int count_names_with(const char *needle) {
    int n = 0;
    DIR *d = opendir(g_dir);
    struct dirent *e;
    while (d && (e = readdir(d)))
        if (strstr(e->d_name, needle)) n++;
    if (d) closedir(d);
    return n;
}

static int test_round_trip_is_nul_safe(void) {
    char p[PATH_MAX];
    uint8_t *data = NULL;
    size_t len = 0;
    fs_provider_t *prov;

    ASSERT(fresh_dir() == 0);
    path_of(p, sizeof p, "blob.bin");
    ASSERT((prov = fs_local_provider_create()) != NULL);
    ASSERT(fs_provider_write_bytes(prov, p, k_blob, sizeof k_blob) == 0);
    ASSERT(fs_provider_read_bytes(prov, p, 64, &data, &len) == 0);
    ASSERT(len == sizeof k_blob && data && memcmp(data, k_blob, len) == 0);
    mem_domain_free(MEM_DOMAIN_FS, data);
    /* atomic replace of the same file, then an empty write */
    {
        const uint8_t b[] = { 0, 1, 0, 2 };
        ASSERT(fs_provider_write_atomic(prov, p, b, sizeof b) == 0);
        ASSERT(fs_provider_read_bytes(prov, p, 64, &data, &len) == 0);
        ASSERT(len == sizeof b && memcmp(data, b, len) == 0);
        mem_domain_free(MEM_DOMAIN_FS, data);
        ASSERT(fs_provider_write_atomic(prov, p, NULL, 0) == 0);
        ASSERT(fs_provider_read_bytes(prov, p, 64, &data, &len) == 0 && len == 0 && data);
        mem_domain_free(MEM_DOMAIN_FS, data);
        ASSERT(fs_provider_write_bytes(prov, p, NULL, 0) == 0);
    }
    fs_provider_destroy(prov);
    rmrf(g_dir);
    return 0;
}

static int test_size_limit_and_missing(void) {
    char p[PATH_MAX], q[PATH_MAX];
    uint8_t *data = (uint8_t *)&data;
    size_t len = 7;
    fs_provider_t *prov;

    ASSERT(fresh_dir() == 0);
    path_of(p, sizeof p, "big.bin");
    path_of(q, sizeof q, "absent.bin");
    ASSERT((prov = fs_local_provider_create()) != NULL);
    ASSERT(fs_provider_write_bytes(prov, p, k_blob, sizeof k_blob) == 0);
    ASSERT(fs_provider_read_bytes(prov, p, sizeof k_blob - 1, &data, &len) == -2);
    ASSERT(data == NULL && len == 0);
    ASSERT(fs_provider_read_bytes(prov, p, sizeof k_blob, &data, &len) == 0 && len == sizeof k_blob);
    mem_domain_free(MEM_DOMAIN_FS, data);
    data = (uint8_t *)&data;
    len = 7;
    errno = 0;
    ASSERT(fs_provider_read_bytes(prov, q, 64, &data, &len) == -3);
    ASSERT(errno == ENOENT && data == NULL && len == 0);
    fs_provider_destroy(prov);
    rmrf(g_dir);
    return 0;
}

static int test_jail_denies_outside_path(void) {
    char outer[] = "/tmp/fs_prov_jail_XXXXXX";
    char inside[PATH_MAX + 16], outside[PATH_MAX], inner[PATH_MAX];
    uint8_t *data = NULL;
    size_t len = 0;
    fs_provider_t *prov;
    char *canon;

    ASSERT(mkdtemp(outer));
    canon = realpath(outer, NULL);
    ASSERT(canon);
    ASSERT(fresh_dir() == 0); /* a second dir outside the jail */
    path_of(outside, sizeof outside, "outside.bin");
    g_vm_mode = 1;
    snprintf(g_vm_root, sizeof g_vm_root, "%s", canon);
    fs_jail_init();
    ASSERT(fs_jail_is_active() == 1);
    snprintf(inner, sizeof inner, "%s/%s", canon, FS_JAIL_HOST_SUBDIR);
    snprintf(inside, sizeof inside, "%s/in.bin", inner);
    ASSERT((prov = fs_local_provider_create()) != NULL);

    ASSERT(fs_provider_write_atomic(prov, inside, k_blob, sizeof k_blob) == 0);
    ASSERT(fs_provider_read_bytes(prov, inside, 64, &data, &len) == 0 && len == sizeof k_blob);
    mem_domain_free(MEM_DOMAIN_FS, data);
    data = NULL;
    errno = 0;
    ASSERT(fs_provider_write_bytes(prov, outside, k_blob, sizeof k_blob) == -1 && errno == EPERM);
    ASSERT(fs_provider_write_atomic(prov, outside, k_blob, sizeof k_blob) == -1);
    ASSERT(!exists(outside));
    {
        char other[PATH_MAX];
        path_of(other, sizeof other, "present.bin");
        FILE *f = fopen(other, "wb");
        ASSERT(f && fclose(f) == 0);
        ASSERT(fs_provider_read_bytes(prov, other, 64, &data, &len) == -1);
        ASSERT(data == NULL && len == 0);
    }
    fs_provider_destroy(prov);
    g_vm_mode = 0;
    g_vm_root[0] = '\0';
    fs_jail_init();
    ASSERT(chdir("/tmp") == 0);
    snprintf(g_cwd, sizeof g_cwd, "/tmp");
    rmrf(canon);
    free(canon);
    rmrf(g_dir);
    return 0;
}

static int test_atomic_leaves_no_part_and_ignores_part_link(void) {
    char p[PATH_MAX], part[PATH_MAX], victim[PATH_MAX];
    uint8_t *data = NULL;
    size_t len = 0;
    fs_provider_t *prov;

    ASSERT(fresh_dir() == 0);
    path_of(p, sizeof p, "doc.bin");
    path_of(part, sizeof part, "doc.bin.part");
    path_of(victim, sizeof victim, "victim");
    ASSERT((prov = fs_local_provider_create()) != NULL);
    ASSERT(fs_provider_write_atomic(prov, p, k_blob, sizeof k_blob) == 0);
    ASSERT(count_names_with(".part") == 0);
    /* a link planted at the staging name must not be written through */
    ASSERT(symlink(victim, part) == 0);
    ASSERT(fs_provider_write_atomic(prov, p, k_blob, sizeof k_blob) == 0);
    ASSERT(count_names_with(".part") == 0);
    ASSERT(!exists(victim));
    ASSERT(fs_provider_read_bytes(prov, p, 64, &data, &len) == 0);
    ASSERT(len == sizeof k_blob && memcmp(data, k_blob, len) == 0);
    mem_domain_free(MEM_DOMAIN_FS, data);
    fs_provider_destroy(prov);
    rmrf(g_dir);
    return 0;
}

static int test_memory_stub_and_null_slots(void) {
    uint8_t *data = (uint8_t *)&data;
    size_t len = 5;
    fs_provider_t *mem = fs_memory_provider_create();
    fs_provider_vtable_t old = { 0 }; /* a vtable that predates the byte slots */
    fs_provider_t legacy = { &old, NULL };

    ASSERT(mem);
    ASSERT(fs_provider_read_bytes(mem, "/x", 8, &data, &len) == -1 && data == NULL && len == 0);
    ASSERT(fs_provider_write_bytes(mem, "/x", k_blob, sizeof k_blob) == -1);
    ASSERT(fs_provider_write_atomic(mem, "/x", k_blob, sizeof k_blob) == -1);
    fs_provider_destroy(mem);

    errno = 0;
    ASSERT(fs_provider_read_bytes(&legacy, "/x", 8, &data, &len) == -1 && errno == ENOSYS);
    ASSERT(data == NULL && len == 0);
    errno = 0;
    ASSERT(fs_provider_write_bytes(&legacy, "/x", k_blob, 1) == -1 && errno == ENOSYS);
    ASSERT(fs_provider_write_atomic(&legacy, "/x", k_blob, 1) == -1);
    ASSERT(fs_provider_read_bytes(NULL, "/x", 8, &data, &len) == -1);
    ASSERT(fs_provider_write_bytes(NULL, "/x", k_blob, 1) == -1);
    ASSERT(fs_provider_write_atomic(NULL, "/x", k_blob, 1) == -1);
    return 0;
}

static int g_saved_events;
static char g_saved_path[256];

static void on_saved(const fs_event_t *ev, void *ud) {
    (void)ud;
    g_saved_events++;
    snprintf(g_saved_path, sizeof g_saved_path, "%s", ev->path);
}

static int deny_read(fs_access_policy_t *pol, const char *u, const char *p) {
    (void)pol; (void)u; (void)p;
    return 0;
}
static int allow_all(fs_access_policy_t *pol, const char *u, const char *p) {
    (void)pol; (void)u; (void)p;
    return 1;
}
static const fs_access_policy_vtable_t k_deny_read_vt = { deny_read, allow_all, allow_all };

/* path_log has no accessor: print it to a temp file through stdout and read that back. */
static int capture_path_log(char *out, size_t cap) {
    char tmp[] = "/tmp/fs_prov_log_XXXXXX";
    int fd = mkstemp(tmp), saved;
    ssize_t n;

    if (fd < 0) return -1;
    fflush(stdout);
    saved = dup(STDOUT_FILENO);
    if (saved < 0 || dup2(fd, STDOUT_FILENO) < 0) { close(fd); unlink(tmp); return -1; }
    path_log_print(0);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    lseek(fd, 0, SEEK_SET);
    n = read(fd, out, cap - 1);
    close(fd);
    unlink(tmp);
    if (n < 0) return -1;
    out[n] = '\0';
    return 0;
}

static int test_facade_hooks(void) {
    char p[PATH_MAX], prot[PATH_MAX];
    uint8_t *data = NULL;
    size_t len = 0;
    fs_provider_t *prov;
    file_manager_service_t *svc;
    fs_access_policy_t *pol;
    fs_protected_policy_t *pp;

    ASSERT(fresh_dir() == 0);
    path_of(p, sizeof p, "f.bin");
    path_of(prot, sizeof prot, "locked.bin");
    path_log_init();
    fs_events_init();
    fs_events_subscribe(FS_EV_FILE_SAVED, on_saved, NULL);
    ASSERT((prov = fs_local_provider_create()) != NULL);
    ASSERT((svc = fm_service_create(prov)) != NULL);
    ASSERT((pol = fs_protected_policy_create()) != NULL);
    pp = (fs_protected_policy_t *)pol;
    fs_protected_policy_add(pp, prot);
    fm_service_set_policy(svc, pol);

    g_saved_events = 0;
    ASSERT(fm_write_bytes(svc, p, k_blob, sizeof k_blob) == 0);
    ASSERT(g_saved_events == 1 && strcmp(g_saved_path, p) == 0);
    ASSERT(fm_write_atomic(svc, p, k_blob, 4) == 0);
    ASSERT(g_saved_events == 2);
    ASSERT(fm_read_bytes(svc, p, 64, &data, &len) == 0 && len == 4 && memcmp(data, k_blob, 4) == 0);
    mem_domain_free(MEM_DOMAIN_FS, data);
    ASSERT(fm_undo_available(svc) == 0); /* bytes calls push no undo */

    /* write policy: denied, nothing written, no event */
    ASSERT(fm_write_atomic(svc, prot, k_blob, sizeof k_blob) == -1);
    ASSERT(fm_write_bytes(svc, prot, k_blob, sizeof k_blob) == -1);
    ASSERT(!exists(prot) && g_saved_events == 2);

    /* a failed write publishes nothing */
    {
        char missing[PATH_MAX];
        path_of(missing, sizeof missing, "no/such/dir/f.bin");
        ASSERT(fm_write_atomic(svc, missing, k_blob, 1) != 0);
        ASSERT(g_saved_events == 2);
    }

    /* the path log saw the successful calls and none of the refused ones */
    {
        char out[4096], want_w[PATH_MAX + 16], want_r[PATH_MAX + 16], bad[PATH_MAX + 16];
        snprintf(want_w, sizeof want_w, "[write] %s", p);
        snprintf(want_r, sizeof want_r, "[read] %s", p);
        snprintf(bad, sizeof bad, "] %s", prot);
        ASSERT(capture_path_log(out, sizeof out) == 0);
        ASSERT(strstr(out, want_w) && strstr(out, want_r) && !strstr(out, bad));
    }

    /* read policy */
    {
        fs_access_policy_t deny = { &k_deny_read_vt, NULL };
        fm_service_set_policy(svc, &deny);
        data = (uint8_t *)&data;
        len = 3;
        ASSERT(fm_read_bytes(svc, p, 64, &data, &len) == -1 && data == NULL && len == 0);
        fm_service_set_policy(svc, pol);
    }
    ASSERT(fm_read_bytes(NULL, p, 64, &data, &len) == -1);
    ASSERT(fm_write_bytes(NULL, p, k_blob, 1) == -1);

    fm_service_destroy(svc);
    fs_protected_policy_destroy(pol);
    fs_provider_destroy(prov);
    fs_events_shutdown();
    rmrf(g_dir);
    return 0;
}

static int bytes_match(fs_provider_t *prov, const char *path, const uint8_t *want, size_t n) {
    uint8_t *data = NULL;
    size_t len = 0;
    int ok;

    if (fs_provider_read_bytes(prov, path, n + 8, &data, &len) != 0)
        return 0;
    ok = data && len == n && memcmp(data, want, n) == 0;
    mem_domain_free(MEM_DOMAIN_FS, data);
    return ok;
}

static int test_null_buffer_creates_nothing(void) {
    char p[PATH_MAX], part[PATH_MAX];
    fs_provider_t *prov;

    ASSERT(fresh_dir() == 0);
    path_of(p, sizeof p, "absent.bin");
    ASSERT(part_path(part, sizeof part, p) == 0);
    ASSERT((prov = fs_local_provider_create()) != NULL);
    ASSERT(fs_provider_write_bytes(prov, p, NULL, 1) != 0);
    ASSERT(!exists(p) && !exists(part));

    /* an existing file is not truncated when the new buffer is missing */
    ASSERT(fs_provider_write_bytes(prov, p, k_blob, sizeof k_blob) == 0);
    ASSERT(fs_provider_write_bytes(prov, p, NULL, sizeof k_blob) != 0);
    ASSERT(bytes_match(prov, p, k_blob, sizeof k_blob));
    ASSERT(!exists(part));
    fs_provider_destroy(prov);
    rmrf(g_dir);
    return 0;
}

static int test_write_bytes_refuses_symlink(void) {
    char outer[] = "/tmp/fs_prov_link_XXXXXX";
    char inside_file[PATH_MAX], inside_link[PATH_MAX];
    char outside_file[PATH_MAX], outside_link[PATH_MAX], part[PATH_MAX];
    const uint8_t oldb[] = { 'o', 'l', 'd', 0x00, 'y' };
    const uint8_t newb[] = { 'n', 'e', 'w', '!' };
    struct stat st;
    fs_provider_t *prov;

    ASSERT(fresh_dir() == 0);
    ASSERT(mkdtemp(outer));
    ASSERT((prov = fs_local_provider_create()) != NULL);

    path_of(inside_file, sizeof inside_file, "kept.bin");
    path_of(inside_link, sizeof inside_link, "kept.link");
    ASSERT(fs_provider_write_bytes(prov, inside_file, oldb, sizeof oldb) == 0);
    ASSERT(symlink(inside_file, inside_link) == 0);
    ASSERT(fs_provider_write_bytes(prov, inside_link, newb, sizeof newb) != 0);
    ASSERT(fs_provider_write_text(prov, inside_link, "replaced") != 0);
    ASSERT(lstat(inside_link, &st) == 0 && S_ISLNK(st.st_mode));
    ASSERT(bytes_match(prov, inside_file, oldb, sizeof oldb));
    ASSERT(part_path(part, sizeof part, inside_link) == 0);
    ASSERT(!exists(part));

    snprintf(outside_file, sizeof outside_file, "%s/target.bin", outer);
    path_of(outside_link, sizeof outside_link, "out.link");
    ASSERT(fs_provider_write_bytes(prov, outside_file, oldb, sizeof oldb) == 0);
    ASSERT(symlink(outside_file, outside_link) == 0);
    ASSERT(fs_provider_write_bytes(prov, outside_link, newb, sizeof newb) != 0);
    ASSERT(fs_provider_write_text(prov, outside_link, "replaced") != 0);
    ASSERT(lstat(outside_link, &st) == 0 && S_ISLNK(st.st_mode));
    ASSERT(bytes_match(prov, outside_file, oldb, sizeof oldb));
    ASSERT(part_path(part, sizeof part, outside_link) == 0);
    ASSERT(!exists(part));

    fs_provider_destroy(prov);
    rmrf(g_dir);
    rmrf(outer);
    return 0;
}

/* The new bytes are staged, then the replace is refused, and the previous file stays. */
static int test_failed_replace_keeps_old_bytes(void) {
    char p[PATH_MAX], link[PATH_MAX], part[PATH_MAX];
    const uint8_t oldb[] = { 'k', 'e', 'p', 't' };
    const uint8_t newb[] = { 'g', 'o', 'n', 'e' };
    struct stat st;
    fs_provider_t *prov;

    ASSERT(fresh_dir() == 0);
    ASSERT((prov = fs_local_provider_create()) != NULL);
    path_of(p, sizeof p, "stay.bin");
    path_of(link, sizeof link, "stay.link");
    ASSERT(fs_provider_write_bytes(prov, p, oldb, sizeof oldb) == 0);
    ASSERT(symlink(p, link) == 0);
    ASSERT(fs_provider_write_bytes(prov, link, newb, sizeof newb) != 0);
    ASSERT(bytes_match(prov, p, oldb, sizeof oldb));
    ASSERT(part_path(part, sizeof part, link) == 0);
    ASSERT(!exists(part));

    /* the staging name is a directory, so the temp cannot be written or renamed */
    path_of(part, sizeof part, "stay.bin.part");
    ASSERT(mkdir(part, 0755) == 0);
    ASSERT(fs_provider_write_bytes(prov, p, newb, sizeof newb) != 0);
    ASSERT(bytes_match(prov, p, oldb, sizeof oldb));
    ASSERT(lstat(p, &st) == 0 && S_ISREG(st.st_mode));
    ASSERT(lstat(part, &st) == 0 && S_ISDIR(st.st_mode));

    fs_provider_destroy(prov);
    rmrf(g_dir);
    return 0;
}

static int test_replace_keeps_mode(void) {
    char p[PATH_MAX], q[PATH_MAX];
    struct stat st;
    fs_provider_t *prov;

    ASSERT(fresh_dir() == 0);
    ASSERT((prov = fs_local_provider_create()) != NULL);
    path_of(p, sizeof p, "shared.bin");
    path_of(q, sizeof q, "new.bin");
    ASSERT(fs_provider_write_bytes(prov, p, k_blob, sizeof k_blob) == 0);
    ASSERT(chmod(p, 0644) == 0);
    ASSERT(fs_provider_write_bytes(prov, p, k_blob, 3) == 0);
    ASSERT(lstat(p, &st) == 0 && (st.st_mode & 07777) == 0644);
    ASSERT(fs_provider_write_text(prov, p, "text") == 0);
    ASSERT(lstat(p, &st) == 0 && (st.st_mode & 07777) == 0644);
    ASSERT(fs_provider_write_bytes(prov, q, k_blob, 1) == 0);
    ASSERT(lstat(q, &st) == 0 && (st.st_mode & 07777) == 0600);
    fs_provider_destroy(prov);
    rmrf(g_dir);
    return 0;
}

int main(void) {
    struct { const char *name; int (*fn)(void); } tests[] = {
        { "round_trip_is_nul_safe", test_round_trip_is_nul_safe },
        { "size_limit_and_missing", test_size_limit_and_missing },
        { "jail_denies_outside_path", test_jail_denies_outside_path },
        { "atomic_leaves_no_part_and_ignores_part_link", test_atomic_leaves_no_part_and_ignores_part_link },
        { "memory_stub_and_null_slots", test_memory_stub_and_null_slots },
        { "facade_hooks", test_facade_hooks },
        { "null_buffer_creates_nothing", test_null_buffer_creates_nothing },
        { "write_bytes_refuses_symlink", test_write_bytes_refuses_symlink },
        { "failed_replace_keeps_old_bytes", test_failed_replace_keeps_old_bytes },
        { "replace_keeps_mode", test_replace_keeps_mode },
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int r = tests[i].fn();
        printf("%s %s\n", r == 0 ? "PASS" : "FAIL", tests[i].name);
        failed += r != 0;
    }
    printf("%d/%zu passed\n", (int)(sizeof tests / sizeof tests[0]) - failed,
           sizeof tests / sizeof tests[0]);
    return failed ? 1 : 0;
}
