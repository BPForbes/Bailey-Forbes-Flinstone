#include "vrt.h"
#include "mem_asm.h"
#include <stdlib.h>
#include <string.h>

static vrt_entry_t s_table[VRT_MAX_HANDLES];
static int s_inited;

void vrt_init(void) {
    if (s_inited) return;
    asm_mem_zero(s_table, sizeof(s_table));
    s_inited = 1;
}

void vrt_shutdown(void) {
    asm_mem_zero(s_table, sizeof(s_table));
    s_inited = 0;
}

vrt_handle_t vrt_alloc(vrt_type_t type, void *resource, const char *path, size_t size) {
    if (!s_inited) return VRT_HANDLE_INVALID;
    for (vrt_handle_t i = 0; i < VRT_MAX_HANDLES; i++) {
        if (s_table[i].resource == NULL) {
            s_table[i].type = type;
            s_table[i].resource = resource;
            s_table[i].size = size;
            if (path)
                strncpy(s_table[i].path, path, sizeof(s_table[i].path) - 1);
            else
                s_table[i].path[0] = '\0';
            return i;
        }
    }
    return VRT_HANDLE_INVALID;
}

void vrt_free(vrt_handle_t h) {
    if (h >= VRT_MAX_HANDLES) return;
    asm_mem_zero(&s_table[h], sizeof(s_table[h]));
}

int vrt_get(vrt_handle_t h, vrt_entry_t *out) {
    if (h >= VRT_MAX_HANDLES || !out || s_table[h].resource == NULL)
        return -1;
    *out = s_table[h];
    return 0;
}

void *vrt_resource(vrt_handle_t h) {
    if (h >= VRT_MAX_HANDLES) return NULL;
    return s_table[h].resource;
}
