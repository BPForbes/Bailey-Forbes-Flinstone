/**
 * Minimal device filesystem API.
 *
 * Provides user-facing handles backed by registered kernel devices.
 */
#ifndef FL_DRIVER_DEVFS_H
#define FL_DRIVER_DEVFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_DEVFS_O_READ   1
#define FL_DEVFS_O_WRITE  2

#define FL_DEVFS_IOCTL_GET_CAPS    1
#define FL_DEVFS_IOCTL_BLOCK_CAPS  FL_DEVFS_IOCTL_GET_CAPS

typedef struct fl_devfs_file {
    void   *node;
    size_t  pos;
    int     flags;
} fl_devfs_file_t;

/* Block device ops: sector-aligned I/O (unit = sector index) */
typedef int (*fl_devfs_read_fn)(void *dev, uint32_t unit, void *buf, size_t len, size_t *read_out);
typedef int (*fl_devfs_write_fn)(void *dev, uint32_t unit, const void *buf, size_t len, size_t *written_out);

/* Character device ops: arbitrary-length byte I/O */
typedef int (*fl_devfs_byte_read_fn)(void *dev, void *buf, size_t len, size_t *read_out);
typedef int (*fl_devfs_byte_write_fn)(void *dev, const void *buf, size_t len, size_t *written_out);

typedef int (*fl_devfs_ioctl_fn)(void *dev, unsigned long request, void *arg);

typedef struct fl_devfs_ops {
    fl_devfs_read_fn       read;        /* block: set for block devices, NULL for char */
    fl_devfs_write_fn      write;
    fl_devfs_byte_read_fn  byte_read;   /* char: set for char devices, NULL for block */
    fl_devfs_byte_write_fn byte_write;
    fl_devfs_ioctl_fn      ioctl;
} fl_devfs_ops_t;

int fl_devfs_register(const char *path, int class_id, void *dev, const fl_devfs_ops_t *ops);
void fl_devfs_unregister(const char *path);
int fl_devfs_open(const char *path, int flags, fl_devfs_file_t *out);
int fl_devfs_read(fl_devfs_file_t *file, void *buf, size_t size, size_t *read_out);
int fl_devfs_write(fl_devfs_file_t *file, const void *buf, size_t size, size_t *written_out);
int fl_devfs_ioctl(fl_devfs_file_t *file, unsigned long request, void *arg);
int fl_devfs_close(fl_devfs_file_t *file);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_DEVFS_H */
