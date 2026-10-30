#ifndef FS_PROVIDER_H
#define FS_PROVIDER_H

#include "fs_types.h"

/* Create providers */
fs_provider_t *fs_local_provider_create(void);
fs_provider_t *fs_memory_provider_create(void);

/* Provider operations (convenience wrappers) */
int fs_provider_list(fs_provider_t *p, const char *path, fs_node_t **out, int *count);
int fs_provider_read_text(fs_provider_t *p, const char *path, char *buf, size_t bufsiz);
int fs_provider_write_text(fs_provider_t *p, const char *path, const char *content);
int fs_provider_create_file(fs_provider_t *p, const char *path);
int fs_provider_create_dir(fs_provider_t *p, const char *path);
int fs_provider_delete(fs_provider_t *p, const char *path);
int fs_provider_move(fs_provider_t *p, const char *src, const char *dst);
/* Byte-exact calls (see fs_types.h). A NULL provider, vtable or slot gives -1 with errno ENOSYS. */
int fs_provider_read_bytes(fs_provider_t *p, const char *path, size_t max, uint8_t **data, size_t *len);
int fs_provider_write_bytes(fs_provider_t *p, const char *path, const uint8_t *data, size_t len);
int fs_provider_write_atomic(fs_provider_t *p, const char *path, const uint8_t *data, size_t len);

/*
 * Contained calls over a held directory (fs_dir_ref_t, fs_types.h). Each answers -1 with errno
 * ENOSYS when the provider has no such slot. Names are plain: no separator, not "." or "..".
 */
int fs_provider_dir_open(fs_provider_t *p, const char *dir, fs_dir_ref_t *out);
void fs_provider_dir_close(fs_provider_t *p, fs_dir_ref_t *d);
int fs_provider_dir_read(fs_provider_t *p, const fs_dir_ref_t *d, const char *name, size_t max,
                         uint8_t **data, size_t *len);
/** 1 present (a link counts), 0 absent, -1 cannot tell. */
int fs_provider_dir_exists(fs_provider_t *p, const fs_dir_ref_t *d, const char *name);
int fs_provider_dir_read_tail(fs_provider_t *p, const fs_dir_ref_t *d, const char *name,
                              size_t unit, size_t keep, uint8_t **data, size_t *len);
/** All of files[0..n) replace or none (n is 1 to 4). 0 on success. */
int fs_provider_dir_commit(fs_provider_t *p, const fs_dir_ref_t *d, const fs_dir_file_t *files,
                           size_t n);
void fs_provider_destroy(fs_provider_t *p);

/* Free node array from list_entries */
void fs_nodes_free(fs_node_t *nodes, int count);

#endif /* FS_PROVIDER_H */
