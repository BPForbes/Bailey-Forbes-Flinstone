#include "identity.h"

struct user {
    char name[16];
    char secret[24];
    int elevated;
    int used;
    int kq; /* 1: password lives in the ops table's seal, not in secret */
};

static struct user s_users[FL_FS_MAX_USERS];
static int s_session_user[FL_FS_MAX_SESSIONS];
static int s_session_used[FL_FS_MAX_SESSIONS];
static int s_session_sudo[FL_FS_MAX_SESSIONS];
static int s_active;
static int s_sessions;
static const fl_fs_identity_kq_ops_t *s_kq;

const fl_fs_identity_kq_ops_t *fl_fs_identity_set_kq(const fl_fs_identity_kq_ops_t *ops)
{
    const fl_fs_identity_kq_ops_t *prev = s_kq;

    s_kq = ops;
    return prev;
}

static void secret_clear(char *secret)
{
    unsigned i;

    for (i = 0; i < sizeof s_users[0].secret; ++i)
        secret[i] = 0;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

/* 1 when password opens this account: the sealed key when KeyQuorum enrolled it, else the
 * stored demo password. A sealed account with no unlock op fails closed. */
static int password_ok(const struct user *u, const char *password)
{
    if (!password)
        return 0;
    if (u->kq)
        return s_kq && s_kq->unlock && s_kq->unlock(u->name, password);
    return str_eq(u->secret, password);
}

/* Account names match regardless of case ("root" finds Root), as on the hosted shell. */
static int str_eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (ca != cb)
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int find_user(const char *name)
{
    for (int i = 0; i < FL_FS_MAX_USERS; ++i) {
        if (s_users[i].used && str_eq_ci(s_users[i].name, name))
            return i;
    }
    return -1;
}

static int add_user(const char *creator, const char *name, const char *password, int elevated)
{
    if (!name || !name[0] || !password || find_user(name) >= 0)
        return -1;
    for (int i = 0; i < FL_FS_MAX_USERS; ++i) {
        if (s_users[i].used)
            continue;
        s_users[i].used = 1;
        s_users[i].elevated = elevated;
        s_users[i].kq = 0;
        str_copy(s_users[i].name, name, sizeof(s_users[i].name));
        secret_clear(s_users[i].secret);
        if (s_kq && s_kq->enroll) {
            /* creator is a name the ops may record; identity does not interpret it.
             * Root's creator is NULL. Flinstone's is "Root" even though Root is added next,
             * so the account indexes (and the lab's perspectives) stay in this order. */
            if (s_kq->enroll(creator, s_users[i].name, password) != 0) {
                s_users[i].used = 0;
                return -1;
            }
            s_users[i].kq = 1;
        } else {
            str_copy(s_users[i].secret, password, sizeof(s_users[i].secret));
        }
        return i;
    }
    return -1;
}

void fl_fs_identity_init(void)
{
    for (int i = 0; i < FL_FS_MAX_USERS; ++i)
        s_users[i].used = 0;
    for (int i = 0; i < FL_FS_MAX_SESSIONS; ++i) {
        s_session_used[i] = 0;
        s_session_user[i] = 0;
        s_session_sudo[i] = 0;
    }
#ifndef FL_SEED_DEFAULT_ACCOUNTS
#define FL_SEED_DEFAULT_ACCOUNTS 1 /* the public lab keeps its demo accounts; a build that must not
                                    * carry known passwords defines this 0 */
#endif
    /* Canonical spellings, as on the hosted shell: Root is the master, Flinstone the default user.
     * Order is the account index the lab perspectives use. */
#if FL_SEED_DEFAULT_ACCOUNTS
    add_user("Root", "Flinstone", "flinstone", 0);
    add_user((const char *)0, "Root", "root", 1);
#endif
    s_session_used[0] = 1;
    s_session_user[0] = find_user("Flinstone");
    s_active = 0;
    s_sessions = 1;
}

const char *fl_fs_identity_user(int session)
{
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return "";
    int idx = s_session_user[session];
    if (idx < 0)
        return "";
    return s_users[idx].name;
}

int fl_fs_identity_elevated(int session)
{
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return 0;
    if (s_session_sudo[session])
        return 1;
    int idx = s_session_user[session];
    return idx >= 0 && s_users[idx].elevated;
}

static int auth(int session, const char *name, const char *password)
{
    int idx = find_user(name);
    if (idx < 0 || !password_ok(&s_users[idx], password))
        return 0;
    s_session_user[session] = idx;
    return 1;
}

int fl_fs_identity_login(int session, const char *name, const char *password)
{
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return 0;
    return auth(session, name, password);
}

int fl_fs_identity_su(int session, const char *name, const char *password)
{
    return fl_fs_identity_login(session, name, password);
}

int fl_fs_identity_switchuser(int session, const char *name)
{
    int idx;
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return 0;
    idx = find_user(name);
    if (idx < 0 || s_session_user[session] < 0)
        return 0;
    /* A switch_allowed op is the relation check. With none installed, switchuser stays open:
     * the lab reconnects a seat with it, and that restriction is not confirmed. */
    if (s_kq && s_kq->switch_allowed &&
        !s_kq->switch_allowed(s_users[s_session_user[session]].name, s_users[idx].name))
        return 0;
    s_session_user[session] = idx;
    return 1;
}

int fl_fs_identity_enter_root(int session)
{
    int idx;

    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return 0;
    idx = find_user("Root");
    if (idx < 0)
        return 0;
    s_session_user[session] = idx;
    return 1;
}

int fl_fs_identity_user_index(int session)
{
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return -1;
    return s_session_user[session];
}

void fl_fs_identity_logout(int session)
{
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return;
    s_session_sudo[session] = 0;
    s_session_user[session] = find_user("Flinstone");
}

int fl_fs_identity_useradd(int session, const char *name, const char *password)
{
    if (!fl_fs_identity_elevated(session))
        return 0;
    return add_user(fl_fs_identity_user(session), name, password, 0) >= 0;
}

int fl_fs_identity_userdel(int session, const char *name)
{
    int idx;
    int guest;
    if (!fl_fs_identity_elevated(session) || !name || !name[0])
        return 0;
    if (str_eq_ci(name, "Flinstone") || str_eq_ci(name, "Root"))
        return 0;
    idx = find_user(name);
    if (idx < 0)
        return 0;
    guest = find_user("Flinstone");
    for (int i = 0; i < FL_FS_MAX_SESSIONS; ++i) {
        if (s_session_used[i] && s_session_user[i] == idx)
            s_session_user[i] = guest;
    }
    if (s_users[idx].kq && s_kq && s_kq->forget)
        s_kq->forget(s_users[idx].name);
    s_users[idx].used = 0;
    secret_clear(s_users[idx].secret);
    s_users[idx].kq = 0;
    return 1;
}

int fl_fs_identity_passwd(int session, const char *name, const char *password)
{
    int idx;
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session] || !password || !password[0])
        return 0;
    idx = (name && name[0]) ? find_user(name) : s_session_user[session];
    if (idx < 0)
        return 0;
    if (idx != s_session_user[session] && !fl_fs_identity_elevated(session))
        return 0;
    if (s_users[idx].kq) {
        if (!s_kq || !s_kq->change_password || s_kq->change_password(s_users[idx].name, password) != 0)
            return 0;
        secret_clear(s_users[idx].secret);
        return 1;
    }
    str_copy(s_users[idx].secret, password, sizeof(s_users[idx].secret));
    return 1;
}

int fl_fs_identity_sudo(int session, const char *password)
{
    int idx;
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session] || !password)
        return 0;
    idx = s_session_user[session];
    if (idx < 0 || !password_ok(&s_users[idx], password))
        return 0;
    s_session_sudo[session] = 1;
    return 1;
}

void fl_fs_identity_sudo_k(int session)
{
    if (session < 0 || session >= FL_FS_MAX_SESSIONS)
        return;
    s_session_sudo[session] = 0;
}

void fl_fs_identity_each_user(void (*visit)(const char *name, int elevated, void *ctx), void *ctx)
{
    for (int i = 0; i < FL_FS_MAX_USERS; ++i) {
        if (s_users[i].used)
            visit(s_users[i].name, s_users[i].elevated, ctx);
    }
}

int fl_fs_session_count(void)
{
    return s_sessions;
}

int fl_fs_session_active(void)
{
    return s_active;
}

int fl_fs_session_new(void)
{
    for (int i = 0; i < FL_FS_MAX_SESSIONS; ++i) {
        if (s_session_used[i])
            continue;
        s_session_used[i] = 1;
        s_session_user[i] = find_user("Flinstone");
        s_session_sudo[i] = 0;
        s_sessions++;
        s_active = i;
        return i;
    }
    return -1;
}

int fl_fs_session_switch(int session)
{
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return 0;
    s_active = session;
    return 1;
}
