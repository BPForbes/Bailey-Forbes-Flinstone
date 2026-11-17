#ifndef FL_FREESTANDING_IDENTITY_H
#define FL_FREESTANDING_IDENTITY_H

#define FL_FS_MAX_SESSIONS 4
#define FL_FS_MAX_USERS 8

typedef struct fl_fs_identity_kq_ops {
    int (*enroll)(const char *creator, const char *name, const char *password);
    int (*unlock)(const char *name, const char *password);
    int (*switch_allowed)(const char *from, const char *to);
    int (*change_password)(const char *name, const char *password);
    void (*forget)(const char *name);
} fl_fs_identity_kq_ops_t;

void fl_fs_identity_set_kq(const fl_fs_identity_kq_ops_t *ops);

void fl_fs_identity_init(void);
const char *fl_fs_identity_user(int session);
int fl_fs_identity_elevated(int session);
int fl_fs_identity_login(int session, const char *name, const char *password);
int fl_fs_identity_su(int session, const char *name, const char *password);
int fl_fs_identity_has_user(const char *name);
int fl_fs_identity_switchuser(int session, const char *name, const char *password);
/* Call only after the sudo password was accepted. */
int fl_fs_identity_enter_root(int session);
int fl_fs_identity_user_index(int session);
void fl_fs_identity_logout(int session);
int fl_fs_identity_useradd(int session, const char *name, const char *password);
int fl_fs_identity_userdel(int session, const char *name);
int fl_fs_identity_passwd(int session, const char *name, const char *password);
int fl_fs_identity_sudo(int session, const char *password);
void fl_fs_identity_sudo_k(int session);
void fl_fs_identity_each_user(void (*visit)(const char *name, int elevated, void *ctx), void *ctx);
int fl_fs_session_count(void);
int fl_fs_session_active(void);
int fl_fs_session_new(void);
int fl_fs_session_switch(int session);

#endif
