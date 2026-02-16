#ifndef FL_VFS_H
#define FL_VFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* File system node types */
typedef enum {
    VFS_TYPE_FILE,
    VFS_TYPE_DIR,
    VFS_TYPE_DEVICE
} vfs_type_t;

/* File system node */
struct vfs_node;
typedef struct vfs_node vfs_node_t;

/* File operations */
typedef struct {
    int (*open)(vfs_node_t *node, int flags);
    int (*close)(vfs_node_t *node);
    ssize_t (*read)(vfs_node_t *node, void *buf, size_t count);
    ssize_t (*write)(vfs_node_t *node, const void *buf, size_t count);
    int (*seek)(vfs_node_t *node, off_t offset, int whence);
} vfs_ops_t;

/* VFS operations */
vfs_node_t *vfs_open(const char *path, int flags);
int vfs_close(vfs_node_t *node);
ssize_t vfs_read(vfs_node_t *node, void *buf, size_t count);
ssize_t vfs_write(vfs_node_t *node, const void *buf, size_t count);
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);

/* Path resolution */
char *vfs_resolve_path(const char *path);
int vfs_normalize_path(char *path, size_t size);

/* VFS initialization */
void vfs_init(void);
void vfs_mount(const char *path, vfs_node_t *root);

#ifdef __cplusplus
}
#endif

#endif /* FL_VFS_H */
