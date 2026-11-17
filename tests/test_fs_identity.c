/* Freestanding identity: login, su and sudo go through the KeyQuorum ops when installed. */
#include "identity.h"
#include "tests/kq_test_util.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

struct held {
    int used;
    char name[16];
    char pw[40];
    int forgotten;
};

static struct held s_held[8];
static int s_unlocks;

static struct held *held_find(const char *name) {
    for (int i = 0; i < 8; i++)
        if (s_held[i].used && strcmp(s_held[i].name, name) == 0)
            return &s_held[i];
    return NULL;
}

static int test_enroll(const char *creator, const char *name, const char *password) {
    struct held *h;

    (void)creator;
    if (!name || !password || held_find(name))
        return -1;
    for (int i = 0; i < 8; i++) {
        if (s_held[i].used)
            continue;
        h = &s_held[i];
        memset(h, 0, sizeof *h);
        snprintf(h->name, sizeof h->name, "%s", name);
        snprintf(h->pw, sizeof h->pw, "%s", password);
        h->used = 1;
        return 0;
    }
    return -1;
}

static int test_unlock(const char *name, const char *password) {
    struct held *h = name ? held_find(name) : NULL;

    s_unlocks++;
    return h && password && strcmp(h->pw, password) == 0;
}

static int test_change(const char *name, const char *password) {
    struct held *h = name ? held_find(name) : NULL;

    if (!h || !password || !password[0])
        return -1;
    snprintf(h->pw, sizeof h->pw, "%s", password);
    return 0;
}

static void test_forget(const char *name) {
    struct held *h = name ? held_find(name) : NULL;

    if (!h)
        return;
    h->forgotten++;
    h->used = 0;
}

static const fl_fs_identity_kq_ops_t s_ops = {
    .enroll = test_enroll,
    .unlock = test_unlock,
    .change_password = test_change,
    .forget = test_forget,
};

static int deny_switch(const char *from, const char *to) {
    (void)from;
    (void)to;
    return 0;
}

static const fl_fs_identity_kq_ops_t s_ops_closed = {
    .enroll = test_enroll,
    .unlock = test_unlock,
    .switch_allowed = deny_switch,
    .change_password = test_change,
    .forget = test_forget,
};

/* No forget op: a sealed account cannot have its key wiped, so it must not be deleted. */
static const fl_fs_identity_kq_ops_t s_ops_noforget = {
    .enroll = test_enroll,
    .unlock = test_unlock,
    .change_password = test_change,
};

static int test_plaintext_then_sealed(void) {
    char pw[17], next[17], wrong[17];
    int unlocks_before;

    memset(s_held, 0, sizeof s_held);
    s_unlocks = 0;
    fl_fs_identity_set_kq(NULL);
    fl_fs_identity_init();

    ASSERT(fl_fs_identity_switchuser(0, "Root", NULL) == 0); /* another account: password needed */
    ASSERT(strcmp(fl_fs_identity_user(0), "Flinstone") == 0);
    ASSERT(fl_fs_identity_switchuser(0, "flinstone", NULL) == 1); /* its own account is free */
    ASSERT(fl_fs_identity_enter_root(0) == 1);
    ASSERT(kq_test_random_hex(pw, 8) == 0);
    ASSERT(fl_fs_identity_useradd(0, "Ada", pw) == 1);
    snprintf(wrong, sizeof wrong, "%s", pw);
    wrong[0] = (char)(wrong[0] == 'a' ? 'b' : 'a');
    ASSERT(fl_fs_identity_switchuser(0, "Ada", wrong) == 0);
    ASSERT(strcmp(fl_fs_identity_user(0), "Root") == 0);
    ASSERT(fl_fs_identity_switchuser(0, "Ada", pw) == 1);
    ASSERT(strcmp(fl_fs_identity_user(0), "Ada") == 0);
    ASSERT(fl_fs_identity_has_user("ADA") == 1 && fl_fs_identity_has_user("nobody") == 0);
    ASSERT(fl_fs_identity_login(0, "Ada", pw) == 1);
    ASSERT(s_unlocks == 0);
    ASSERT(fl_fs_identity_login(0, "Ada", wrong) == 0);
    ASSERT(strcmp(fl_fs_identity_user(0), "Ada") == 0);

    memset(s_held, 0, sizeof s_held);
    fl_fs_identity_set_kq(&s_ops);
    fl_fs_identity_init();
    ASSERT(held_find("Flinstone") && held_find("Root"));
    ASSERT(fl_fs_identity_enter_root(0) == 1);
    ASSERT(kq_test_random_hex(pw, 8) == 0);
    unlocks_before = s_unlocks;
    ASSERT(fl_fs_identity_useradd(0, "Ada", pw) == 1);
    ASSERT(held_find("Ada") != NULL);
    unlocks_before = s_unlocks;
    ASSERT(fl_fs_identity_switchuser(0, "Ada", pw) == 1);
    ASSERT(s_unlocks > unlocks_before);
    ASSERT(fl_fs_identity_enter_root(0) == 1);
    unlocks_before = s_unlocks;
    ASSERT(fl_fs_identity_login(0, "Ada", pw) == 1);
    ASSERT(s_unlocks > unlocks_before);
    ASSERT(strcmp(fl_fs_identity_user(0), "Ada") == 0);
    ASSERT(fl_fs_identity_sudo(0, pw) == 1);
    ASSERT(fl_fs_identity_elevated(0) == 1);
    fl_fs_identity_sudo_k(0);
    ASSERT(fl_fs_identity_sudo(0, wrong) == 0);

    ASSERT(kq_test_random_hex(next, 8) == 0);
    ASSERT(strcmp(pw, next) != 0);
    ASSERT(fl_fs_identity_passwd(0, NULL, next) == 1);
    ASSERT(fl_fs_identity_login(0, "Ada", pw) == 0);
    ASSERT(fl_fs_identity_login(0, "Ada", next) == 1);

/* switch_allowed may refuse even with the right Root password; sudo -i does not ask it. */
    ASSERT(test_unlock("Root", "root") == 1);
    fl_fs_identity_set_kq(&s_ops_closed);
    ASSERT(fl_fs_identity_switchuser(0, "Root", "root") == 0);
    ASSERT(strcmp(fl_fs_identity_user(0), "Ada") == 0);
    ASSERT(fl_fs_identity_enter_root(0) == 1);
    ASSERT(strcmp(fl_fs_identity_user(0), "Root") == 0);

    fl_fs_identity_set_kq(&s_ops_noforget);
    ASSERT(fl_fs_identity_userdel(0, "Ada") == 0);
    ASSERT(fl_fs_identity_has_user("Ada") == 1);
    ASSERT(held_find("Ada") != NULL);
    ASSERT(fl_fs_identity_login(0, "Ada", next) == 1);
    ASSERT(fl_fs_identity_enter_root(0) == 1);
    fl_fs_identity_set_kq(&s_ops_closed);

    ASSERT(fl_fs_identity_userdel(0, "Ada") == 1);
    ASSERT(held_find("Ada") == NULL);
    ASSERT(fl_fs_identity_has_user("Ada") == 0);
    {
        int forgotten = 0;
        for (int i = 0; i < 8; i++)
            forgotten += s_held[i].forgotten;
        ASSERT(forgotten == 1);
    }
    ASSERT(fl_fs_identity_useradd(0, "Ada", next) == 1);
    ASSERT(held_find("Ada") != NULL);
    return 0;
}

int main(void) {
    if (test_plaintext_then_sealed() != 0)
        return 1;
    printf("test_fs_identity... OK\n");
    return 0;
}
