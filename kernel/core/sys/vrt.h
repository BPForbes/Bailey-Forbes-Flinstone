/**
 * Virtual Resource Table - map handles to resources.
 * Enables swapping implementations and namespace isolation.
 */
#ifndef VRT_H
#define VRT_H

#include <stddef.h>
#include <stdint.h>

#define VRT_MAX_HANDLES  256
#define VRT_HANDLE_INVALID  ((vrt_handle_t)-1)

typedef uintptr_t vrt_handle_t;

typedef enum {
    VRT_TYPE_FILE,
    VRT_TYPE_DIR,
    VRT_TYPE_BLOCK,
    VRT_TYPE_BUFFER,
} vrt_type_t;

typedef struct vrt_entry {
    vrt_type_t type;
    void *resource;      /* path, ptr, or driver */
    char path[256];      /* for file/dir: canonical path */
    size_t size;         /* for buffer: size */
} vrt_entry_t;

void vrt_init(void);
void vrt_shutdown(void);
vrt_handle_t vrt_alloc(vrt_type_t type, void *resource, const char *path, size_t size);
void vrt_free(vrt_handle_t h);
int vrt_get(vrt_handle_t h, vrt_entry_t *out);
void *vrt_resource(vrt_handle_t h);

#endif /* VRT_H */
