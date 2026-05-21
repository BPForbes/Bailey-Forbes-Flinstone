#include "fl_stack.h"
#include "exec_context.h"
#include "timekeeping.h"
#include "user_db.h"
#include "session.h"
#include "elevation.h"
#include "path_property.h"
#include "pmm.h"
#include "contract_result.h"
#include "fl/authz_subsystem.h"
#include "fl_shell_cmd.h"
#include "contract_p2_authz.h"
#include "contract_p2_principal_names.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_fl_stack(void) {
    uintptr_t buf[4];
    fl_stack_t s;
    uintptr_t v;
    ASSERT(fl_stack_init(&s, buf, 4) == 0);
    ASSERT(fl_stack_push(&s, 10) == 0);
    ASSERT(fl_stack_push(&s, 20) == 0);
    ASSERT(fl_stack_pop(&s, &v) == 0 && v == 20);
    ASSERT(fl_stack_pop(&s, &v) == 0 && v == 10);
    return 0;
}

static int test_exec_context(void) {
    fl_exec_context_t ctx;
    uintptr_t w = 0;
    void *heap = NULL;
    ASSERT(fl_exec_context_create(&ctx, 8192, 4096) == FL_RESULT_OK);
    ASSERT(fl_exec_context_push_stack(&ctx, 42) == FL_RESULT_OK);
    ASSERT(fl_exec_context_pop_stack(&ctx, &w) == FL_RESULT_OK && w == 42);
    ASSERT(fl_exec_context_heap_alloc(&ctx, 64, &heap) == FL_RESULT_OK && heap != NULL);
    fl_exec_context_destroy(&ctx);
    return 0;
}

static int test_timekeeping(void) {
    int64_t a = 0, b = 0;
    ASSERT(fl_time_monotonic_ns(&a) == FL_RESULT_OK);
    ASSERT(fl_time_wall_sec(&b) == FL_RESULT_OK);
    ASSERT(a > 0 && b > 0);
    return 0;
}

static int test_user_db(void) {
    char tmpl[] = "/tmp/fl_users_testXXXXXX";
    char dbpath[sizeof(tmpl) + 8];
    fl_user_db_t db;
    const fl_user_record_t *r;
    int fd;

    fd = mkstemp(tmpl);
    ASSERT(fd >= 0);
    close(fd);
    snprintf(dbpath, sizeof(dbpath), "%s.db", tmpl);
    ASSERT(rename(tmpl, dbpath) == 0);

    ASSERT(fl_user_db_load(&db, dbpath) == FL_RESULT_OK);
    ASSERT(db.count >= 2);
    ASSERT(fl_user_db_verify_password(&db, "flinstone", "flinstone") == 1);
    ASSERT(fl_user_db_verify_password(&db, "root", "root") == 1);
    ASSERT(fl_user_db_verify_password(&db, "flinstone", "wrong") == 0);
    r = fl_user_db_find(&db, "root");
    ASSERT(r && r->is_elevated == 1);
    ASSERT(fl_user_db_is_elevated_user(&db, "root") == 1);
    unlink(dbpath);
    return 0;
}

static int test_path_property(void) {
    char tmpl[] = "/tmp/fl_propXXXXXX";
    char dir[256];
    fl_path_property_t prop;
    if (!mkdtemp(tmpl))
        return 1;
    snprintf(dir, sizeof(dir), "%s/sub", tmpl);
    ASSERT(mkdir(dir, 0755) == 0);
    ASSERT(fl_path_property_set_dir(dir, "user", 1) == FL_RESULT_OK);
    ASSERT(fl_path_property_resolve(dir, &prop) == FL_RESULT_OK);
    ASSERT(prop.requires_elevation == 1);
    return 0;
}

static int test_session_login(void) {
    char tmpl[] = "/tmp/fl_sess_testXXXXXX";
    char dbpath[sizeof(tmpl) + 8];
    int fd;

    fd = mkstemp(tmpl);
    ASSERT(fd >= 0);
    close(fd);
    snprintf(dbpath, sizeof(dbpath), "%s.db", tmpl);
    ASSERT(rename(tmpl, dbpath) == 0);

    setenv("FL_USERS_DB_PATH", dbpath, 1);
    fl_session_init();
    ASSERT(fl_session_login("root", "root") == FL_RESULT_OK);
    ASSERT(fl_session_is_elevated_account() == 1);
    ASSERT(fl_session_has_elevation() == 1);
    ASSERT(fl_session_jail_privileged() == 1);
    ASSERT(fl_session_login("flinstone", "flinstone") == FL_RESULT_OK);
    ASSERT(fl_session_is_elevated_account() == 0);
    ASSERT(fl_session_has_elevation() == 0);
    ASSERT(fl_session_verify_password("flinstone") == 1);
    ASSERT(fl_session_verify_password("wrong") == 0);
    ASSERT(fl_session_jail_privileged() == 0);
    {
        fl_elevation_token_t tok = FL_ELEVATION_TOKEN_NONE;
        ASSERT(fl_session_grant_elevation("test", &tok) == FL_RESULT_OK);
        ASSERT(fl_session_has_elevation() == 1);
        ASSERT(fl_session_jail_privileged() == 0);
        fl_session_clear_elevation();
    }
    fl_session_begin_sudo_scope();
    ASSERT(fl_session_jail_privileged() == 1);
    ASSERT(fl_session_in_sudo_scope() == 1);
    fl_session_end_sudo_scope();
    ASSERT(fl_session_jail_privileged() == 0);
    unlink(dbpath);
    return 0;
}

static int test_authz_guest_privileged_ops(void) {
    char prev[256];
    const char *old = getenv(FL_PRINCIPAL_ENV_NAME);

    if (old) {
        strncpy(prev, old, sizeof prev - 1);
        prev[sizeof prev - 1] = '\0';
    }
    (void)setenv(FL_PRINCIPAL_ENV_NAME, FL_PRINCIPAL_GUEST_LITERAL, 1);

    ASSERT(fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_VFS_PRIVILEGED, NULL) ==
           FL_AUTHZ_DENY);
    ASSERT(fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_NETDEV_REGISTER, NULL) ==
           FL_AUTHZ_DENY);
    ASSERT(fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_DISK_LOW_LEVEL, NULL) ==
           FL_AUTHZ_DENY);
    ASSERT(fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_UNSPECIFIED, NULL) ==
           FL_AUTHZ_ALLOW);

    ASSERT(fl_authz_subsystem_op_for_shell_cmd((unsigned)FL_SCMD_FORMAT) ==
           (unsigned)FL_AUTHZ_OP_DISK_LOW_LEVEL);
    ASSERT(fl_authz_subsystem_op_for_shell_cmd((unsigned)FL_SCMD_SETDISK) ==
           (unsigned)FL_AUTHZ_OP_DISK_LOW_LEVEL);
    ASSERT(fl_authz_subsystem_op_for_shell_cmd((unsigned)FL_SCMD_CREATEDISK) ==
           (unsigned)FL_AUTHZ_OP_DISK_LOW_LEVEL);

    if (old)
        (void)setenv(FL_PRINCIPAL_ENV_NAME, prev, 1);
    else
        (void)unsetenv(FL_PRINCIPAL_ENV_NAME);
    return 0;
}

static int test_pmm_stack(void) {
    uintptr_t phys = 0;
    size_t before, after;
    before = pmm_free_count();
    ASSERT(pmm_alloc_frame_result(&phys) == FL_RESULT_OK && phys != 0);
    after = pmm_free_count();
    ASSERT(after + 1 == before);
    ASSERT(pmm_free_frame_result(phys) == FL_RESULT_OK);
    ASSERT(pmm_free_count() == before);
    return 0;
}

int main(void) {
    if (test_fl_stack()) return 1;
    if (test_exec_context()) return 1;
    if (test_timekeeping()) return 1;
    if (test_user_db()) return 1;
    if (test_session_login()) return 1;
    if (test_path_property()) return 1;
    if (test_authz_guest_privileged_ops()) return 1;
    if (test_pmm_stack()) return 1;
    printf("test_p0_p2_wiring: ok\n");
    return 0;
}
