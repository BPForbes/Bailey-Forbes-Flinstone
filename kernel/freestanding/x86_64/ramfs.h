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
int fl_fs_ramfs_dir(int session, const char *path,
                    void (*visit)(const char *name, int is_dir, unsigned size, void *ctx),
                    void *ctx);

#endif
