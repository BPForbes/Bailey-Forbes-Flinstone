#ifndef FL_FREESTANDING_IDENTITY_H
#define FL_FREESTANDING_IDENTITY_H

#define FL_FS_MAX_SESSIONS 4
#define FL_FS_MAX_USERS 8

/*
 * Optional KeyQuorum account ops. NULL (the QEMU image and the host smoke binary) keeps the
 * demo passwords in the account table. The WebAssembly lab installs a table before
 * fl_fs_identity_init: enroll seals a signing key and a box key under the password through the
 * driver, and identity then stores no password. unlock is 1 when that seal opens. switch_allowed
 * is consulted only when it is non-NULL; leaving it NULL keeps password-free switchuser, which
 * the lab still uses until that restriction is confirmed. change_password re-seals (0 on
 * success). forget drops the sealed keys at userdel.
 */
typedef struct fl_fs_identity_kq_ops {
    int (*enroll)(const char *creator, const char *name, const char *password);
    int (*unlock)(const char *name, const char *password);
    int (*switch_allowed)(const char *from, const char *to);
    int (*change_password)(const char *name, const char *password);
    void (*forget)(const char *name);
} fl_fs_identity_kq_ops_t;

const fl_fs_identity_kq_ops_t *fl_fs_identity_set_kq(const fl_fs_identity_kq_ops_t *ops);

void fl_fs_identity_init(void);
const char *fl_fs_identity_user(int session);
int fl_fs_identity_elevated(int session);
int fl_fs_identity_login(int session, const char *name, const char *password);
int fl_fs_identity_su(int session, const char *name, const char *password);
int fl_fs_identity_switchuser(int session, const char *name);
/* sudo -i, after the sudo password was accepted: become Root with no second check. */
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
