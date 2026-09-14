#ifndef FL_FREESTANDING_IDENTITY_H
#define FL_FREESTANDING_IDENTITY_H

#define FL_FS_MAX_SESSIONS 4
#define FL_FS_MAX_USERS 8

void fl_fs_identity_init(void);
const char *fl_fs_identity_user(int session);
int fl_fs_identity_elevated(int session);
int fl_fs_identity_login(int session, const char *name, const char *password);
int fl_fs_identity_su(int session, const char *name, const char *password);
void fl_fs_identity_logout(int session);
int fl_fs_identity_useradd(int session, const char *name, const char *password);
void fl_fs_identity_each_user(void (*visit)(const char *name, int elevated, void *ctx), void *ctx);
int fl_fs_session_count(void);
int fl_fs_session_active(void);
int fl_fs_session_new(void);
int fl_fs_session_switch(int session);

#endif
