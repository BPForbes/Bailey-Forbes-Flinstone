#ifndef FL_FREESTANDING_RAMFS_H
#define FL_FREESTANDING_RAMFS_H

#define FL_FS_RAMFS_PATH 64
#define FL_FS_RAMFS_DATA 160

void fl_fs_ramfs_init(void);
void fl_fs_ramfs_pwd(int session, char *out, unsigned cap);
int fl_fs_ramfs_cd(int session, const char *path);
int fl_fs_ramfs_mkdir(int session, const char *path);
int fl_fs_ramfs_write(int session, const char *path, const char *text);
int fl_fs_ramfs_cat(int session, const char *path, char *out, unsigned cap);
int fl_fs_ramfs_rm(int session, const char *path);
int fl_fs_ramfs_rmdir(int session, const char *path);
int fl_fs_ramfs_rmtree(int session, const char *path);
int fl_fs_ramfs_mv(int session, const char *src, const char *dst);
int fl_fs_ramfs_search(int session, const char *text,
                       void (*visit)(const char *path, void *ctx), void *ctx);
int fl_fs_ramfs_du(int session, const char *path, unsigned *bytes, unsigned *nodes);
int fl_fs_ramfs_dir(int session, const char *path,
                    void (*visit)(const char *name, int is_dir, unsigned size, void *ctx),
                    void *ctx);
int fl_fs_ramfs_listdirs(int session, const char *path,
                         void (*visit)(const char *name, int is_dir, unsigned size, void *ctx),
                         void *ctx);

#endif
