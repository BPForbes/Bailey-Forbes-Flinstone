/**
 * Issue #219 / #220: batch token arity helpers and sh.c clamp integration.
 * Each cmd_*.c object is built with -ffunction-sections so only *_batch_tokens_count
 * is linked (not cmd_*_run).
 */
#include "cmd_decl.h"
#include "cmd_batch.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int batch_argv_clamp_tokens(int tokens_count, int argc, int i) {
    if (tokens_count > argc - i)
        tokens_count = argc - i;
    return tokens_count;
}

static int test_issue220_1_addcluster(void) {
    char *av[] = {"p", "addcluster", "data", "tail"};
    ASSERT(cmd_addcluster_batch_tokens_count(4, av, 1) == 3);
    char *av2[] = {"p", "addcluster"};
    ASSERT(cmd_addcluster_batch_tokens_count(2, av2, 1) == 1);
    return 0;
}

static int test_issue220_2_createdisk(void) {
    char *cd[] = {"p", "createdisk", "vol", "8", "16", "-y", "nextcmd"};
    ASSERT(cmd_createdisk_batch_tokens_count(7, cd, 1) == 5);
    char *cd2[] = {"p", "createdisk", "vol", "8", "16"};
    ASSERT(cmd_createdisk_batch_tokens_count(5, cd2, 1) == 4);
    /* Incomplete: return all trailing tokens so sh.c insufficient-args path skips them. */
    char *short_cd[] = {"p", "createdisk", "vol"};
    ASSERT(cmd_createdisk_batch_tokens_count(3, short_cd, 1) == 2);
    char *only_cd[] = {"p", "createdisk"};
    ASSERT(cmd_createdisk_batch_tokens_count(2, only_cd, 1) == 1);
    return 0;
}

static int test_issue220_3_rmdir(void) {
    char *rd[] = {"p", "rmdir"};
    ASSERT(cmd_rmdir_batch_tokens_count(2, rd, 1) == 0);
    char *rd2[] = {"p", "rmdir", "dir"};
    ASSERT(cmd_rmdir_batch_tokens_count(3, rd2, 1) == 2);
    return 0;
}

static int test_issue220_4_rmtree(void) {
    char *rt[] = {"p", "rmtree"};
    ASSERT(cmd_rmtree_batch_tokens_count(2, rt, 1) == 0);
    char *rt2[] = {"p", "rmtree", "dir"};
    ASSERT(cmd_rmtree_batch_tokens_count(3, rt2, 1) == 2);
    return 0;
}

static int test_issue220_5_setdisk(void) {
    char *sd[] = {"p", "setdisk", "a.img", "swallowed"};
    ASSERT(cmd_setdisk_batch_tokens_count(4, sd, 1) == 2);
    return 0;
}

static int test_issue220_6_su(void) {
    char *su[] = {"p", "su", "alice", "-c", "id", "whoami"};
    ASSERT(cmd_su_batch_tokens_count(6, su, 1) == 2);
    char *su2[] = {"p", "su", "-c", "id"};
    ASSERT(cmd_su_batch_tokens_count(4, su2, 1) == 3);
    return 0;
}

static int test_issue220_8_diskput(void) {
    char *dp[] = {"p", "diskput", "host.bin", "help"};
    ASSERT(cmd_diskput_batch_tokens_count(4, dp, 1) == 3);
    return 0;
}

static int test_issue220_7_useracct_helpers(void) {
    char *ua[] = {"p", "useradd", "bob"};
    ASSERT(cmd_useradd_batch_tokens_count(3, ua, 1) == 2);
    char *ud[] = {"p", "userdel", "bob"};
    ASSERT(cmd_userdel_batch_tokens_count(3, ud, 1) == 2);
    char *pw[] = {"p", "passwd", "bob"};
    ASSERT(cmd_passwd_batch_tokens_count(3, pw, 1) >= 1);
    return 0;
}

/* Issue #204: login/sudo/su spans must not exceed argc-i after sh.c clamp. */
static int test_issue204_batch_clamp_login_sudo_su(void) {
    int tc;
    char *login_short[] = {"p", "login"};
    tc = cmd_login_batch_tokens_count(2, login_short, 1);
    tc = batch_argv_clamp_tokens(tc, 2, 1);
    ASSERT(tc == 1);

    char *sudo_only[] = {"p", "sudo"};
    tc = cmd_sudo_batch_tokens_count(2, sudo_only, 1);
    tc = batch_argv_clamp_tokens(tc, 2, 1);
    ASSERT(tc == 1);

    char *sudo_flag[] = {"p", "sudo", "-i"};
    ASSERT(cmd_sudo_batch_tokens_count(3, sudo_flag, 1) == 2);

    char *su_tail[] = {"p", "su", "alice", "-c", "id", "whoami"};
    tc = cmd_su_batch_tokens_count(6, su_tail, 1);
    tc = batch_argv_clamp_tokens(tc, 6, 1);
    ASSERT(tc == 2);
    ASSERT(tc <= 6 - 1);
    return 0;
}

int main(void) {
    printf("test_issue220_1_addcluster... ");
    if (test_issue220_1_addcluster() != 0) return 1;
    printf("OK\n");

    printf("test_issue220_2_createdisk... ");
    if (test_issue220_2_createdisk() != 0) return 1;
    printf("OK\n");

    printf("test_issue220_3_rmdir... ");
    if (test_issue220_3_rmdir() != 0) return 1;
    printf("OK\n");

    printf("test_issue220_4_rmtree... ");
    if (test_issue220_4_rmtree() != 0) return 1;
    printf("OK\n");

    printf("test_issue220_5_setdisk... ");
    if (test_issue220_5_setdisk() != 0) return 1;
    printf("OK\n");

    printf("test_issue220_6_su... ");
    if (test_issue220_6_su() != 0) return 1;
    printf("OK\n");

    printf("test_issue220_8_diskput... ");
    if (test_issue220_8_diskput() != 0) return 1;
    printf("OK\n");

    printf("test_issue220_7_useracct_helpers... ");
    if (test_issue220_7_useracct_helpers() != 0) return 1;
    printf("OK\n");

    printf("test_issue204_batch_clamp_login_sudo_su... ");
    if (test_issue204_batch_clamp_login_sudo_su() != 0) return 1;
    printf("OK\n");

    printf("All batch argv issue #220 tests passed.\n");
    return 0;
}
