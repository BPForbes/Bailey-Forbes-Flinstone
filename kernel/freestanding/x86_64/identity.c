#include "identity.h"

struct user {
    char name[16];
    char secret[24];
    int elevated;
    int used;
};

static struct user s_users[FL_FS_MAX_USERS];
static int s_session_user[FL_FS_MAX_SESSIONS];
static int s_session_used[FL_FS_MAX_SESSIONS];
static int s_active;
static int s_sessions;

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
        if (s_users[i].used && str_eq(s_users[i].name, name))
            return i;
    }
    return -1;
}

static int add_user(const char *name, const char *password, int elevated)
{
    if (!name || !name[0] || !password || find_user(name) >= 0)
        return -1;
    for (int i = 0; i < FL_FS_MAX_USERS; ++i) {
        if (s_users[i].used)
            continue;
        s_users[i].used = 1;
        s_users[i].elevated = elevated;
        str_copy(s_users[i].name, name, sizeof(s_users[i].name));
        str_copy(s_users[i].secret, password, sizeof(s_users[i].secret));
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
    }
    add_user("flinstone", "flinstone", 0);
    add_user("root", "root", 1);
    s_session_used[0] = 1;
    s_session_user[0] = find_user("flinstone");
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
    int idx = s_session_user[session];
    return idx >= 0 && s_users[idx].elevated;
}

static int auth(int session, const char *name, const char *password)
{
    int idx = find_user(name);
    if (idx < 0 || !str_eq(s_users[idx].secret, password))
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

void fl_fs_identity_logout(int session)
{
    if (session < 0 || session >= FL_FS_MAX_SESSIONS || !s_session_used[session])
        return;
    s_session_user[session] = find_user("flinstone");
}

int fl_fs_identity_useradd(int session, const char *name, const char *password)
{
    if (!fl_fs_identity_elevated(session))
        return 0;
    return add_user(name, password, 0) >= 0;
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
        s_session_user[i] = find_user("flinstone");
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
