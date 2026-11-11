/*
 * Freestanding identity (FL-14): with no KeyQuorum ops the demo password is checked as before.
 * With ops installed, login, su and sudo ask unlock, passwd re-seals, userdel forgets, and the
 * password the test drew is not what a later unlock sees after passwd. switch_allowed is optional:
 * left NULL, switchuser still changes account; when it refuses, sudo -i still enters Root.
 * Passwords are drawn at run time.
 */
#include "identity.h"

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

static int draw_pw(char *out, size_t cap) {
    FILE *fp = fopen("/dev/urandom", "rb");
    unsigned char raw[16];

    if (!fp || fread(raw, 1, sizeof raw, fp) != sizeof raw) {
        if (fp)
            fclose(fp);
        return -1;
    }
    fclose(fp);
    for (size_t i = 0; i + 1 < cap && i < sizeof raw; i++)
        out[i] = (char)('a' + (raw[i] % 26));
    out[cap - 1] = 0;
    return out[0] ? 0 : -1;
}

static int test_plaintext_then_sealed(void) {
    char pw[16], next[16], wrong[16];
    int unlocks_before;

    memset(s_held, 0, sizeof s_held);
    s_unlocks = 0;
    (void)fl_fs_identity_set_kq(NULL);
    fl_fs_identity_init();

    /* No ops: a drawn password is the account's own check, and unlock is not called. */
    ASSERT(fl_fs_identity_switchuser(0, "Root") == 1);
    ASSERT(draw_pw(pw, sizeof pw) == 0);
    ASSERT(fl_fs_identity_useradd(0, "Ada", pw) == 1);
    ASSERT(fl_fs_identity_login(0, "Ada", pw) == 1);
    ASSERT(s_unlocks == 0);
    snprintf(wrong, sizeof wrong, "%s", pw);
    wrong[0] = (char)(wrong[0] == 'a' ? 'b' : 'a');
    ASSERT(fl_fs_identity_login(0, "Ada", wrong) == 0);
    ASSERT(strcmp(fl_fs_identity_user(0), "Ada") == 0);

    /* Ops installed before the accounts are seeded: login goes through unlock. */
    memset(s_held, 0, sizeof s_held);
    (void)fl_fs_identity_set_kq(&s_ops);
    fl_fs_identity_init();
    ASSERT(held_find("Flinstone") && held_find("Root"));
    ASSERT(fl_fs_identity_switchuser(0, "Root") == 1);
    ASSERT(draw_pw(pw, sizeof pw) == 0);
    unlocks_before = s_unlocks;
    ASSERT(fl_fs_identity_useradd(0, "Ada", pw) == 1);
    ASSERT(held_find("Ada") != NULL);
    unlocks_before = s_unlocks;
    ASSERT(fl_fs_identity_login(0, "Ada", pw) == 1);
    ASSERT(s_unlocks > unlocks_before);
    ASSERT(strcmp(fl_fs_identity_user(0), "Ada") == 0);
    ASSERT(fl_fs_identity_sudo(0, pw) == 1);
    ASSERT(fl_fs_identity_elevated(0) == 1);
    fl_fs_identity_sudo_k(0);
    ASSERT(fl_fs_identity_sudo(0, wrong) == 0);

    /* After passwd, only the new password opens the seal. */
    ASSERT(draw_pw(next, sizeof next) == 0);
    ASSERT(strcmp(pw, next) != 0);
    ASSERT(fl_fs_identity_passwd(0, NULL, next) == 1);
    ASSERT(fl_fs_identity_login(0, "Ada", pw) == 0);
    ASSERT(fl_fs_identity_login(0, "Ada", next) == 1);

    /* switch_allowed may refuse. sudo -i does not ask it. */
    (void)fl_fs_identity_set_kq(&s_ops_closed);
    ASSERT(fl_fs_identity_switchuser(0, "Root") == 0);
    ASSERT(strcmp(fl_fs_identity_user(0), "Ada") == 0);
    ASSERT(fl_fs_identity_enter_root(0) == 1);
    ASSERT(strcmp(fl_fs_identity_user(0), "Root") == 0);

    ASSERT(fl_fs_identity_userdel(0, "Ada") == 1);
    ASSERT(held_find("Ada") == NULL);
    {
        int forgotten = 0;
        for (int i = 0; i < 8; i++)
            forgotten += s_held[i].forgotten;
        ASSERT(forgotten == 1);
    }
    return 0;
}

int main(void) {
    if (test_plaintext_then_sealed() != 0)
        return 1;
    printf("test_fs_identity... OK\n");
    return 0;
}
