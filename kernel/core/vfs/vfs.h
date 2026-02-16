/**
 * VFS - virtual file system interface.
 * Enables host_vfs, memory_vfs, cluster_vfs backends.
 */
#ifndef VFS_H
#define VFS_H

#include <stddef.h>

#define VFS_PATH_MAX 256

typedef struct vfs vfs_t;

typedef struct vfs_ops {
    int (*open)(vfs_t *vfs, const char *path, void **handle_out);
    int (*read)(vfs_t *vfs, void *handle, void *buf, size_t size, size_t *read_out);
    int (*write)(vfs_t *vfs, void *handle, const void *buf, size_t size, size_t *written_out);
    int (*close)(vfs_t *vfs, void *handle);
    int (*list)(vfs_t *vfs, const char *path, char *names, size_t names_len, int *count_out);
    void (*destroy)(vfs_t *vfs);
} vfs_ops_t;

struct vfs {
    const vfs_ops_t *ops;
    void *ctx;
};

vfs_t *vfs_host_create(void *fs_provider);
vfs_t *vfs_memory_create(void);
void vfs_destroy(vfs_t *vfs);

#endif /* VFS_H */
