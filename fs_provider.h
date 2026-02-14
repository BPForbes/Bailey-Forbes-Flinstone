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
void fs_provider_destroy(fs_provider_t *p);

/* Free node array from list_entries */
void fs_nodes_free(fs_node_t *nodes, int count);

#endif /* FS_PROVIDER_H */
