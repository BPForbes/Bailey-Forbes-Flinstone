#include "vfs.h"
#include "fs_provider.h"
#include "fs_types.h"
#include "mem_domain.h"
#include <stdlib.h>
#include <string.h>

/* Host VFS - wraps fs_provider_t */
typedef struct {
    vfs_t base;
    fs_provider_t *provider;
} vfs_host_ctx_t;

static int host_open(vfs_t *vfs, const char *path, void **handle_out) {
    (void)vfs;
    if (!path || !handle_out) return -1;
    *handle_out = (void *)path;  /* use path as handle for read/write */
    return 0;
}

static int host_read(vfs_t *vfs, void *handle, void *buf, size_t size, size_t *read_out) {
    vfs_host_ctx_t *ctx = (vfs_host_ctx_t *)vfs->ctx;
    int n = fs_provider_read_text(ctx->provider, (const char *)handle, buf, size);
    if (n < 0) return -1;
    if (read_out) *read_out = (size_t)n;
    return 0;
}

static int host_write(vfs_t *vfs, void *handle, const void *buf, size_t size, size_t *written_out) {
    vfs_host_ctx_t *ctx = (vfs_host_ctx_t *)vfs->ctx;
    char *str = mem_domain_alloc(MEM_DOMAIN_FS, size + 1);
    if (!str) return -1;
    mem_domain_copy(str, buf, size);
    str[size] = '\0';
    int r = fs_provider_write_text(ctx->provider, (const char *)handle, str);
    mem_domain_free(MEM_DOMAIN_FS, str);
    if (r != 0) return -1;
    if (written_out) *written_out = size;
    return 0;
}

static int host_close(vfs_t *vfs, void *handle) {
    (void)vfs;
    (void)handle;
    return 0;
}

static int host_list(vfs_t *vfs, const char *path, char *names, size_t names_len, int *count_out) {
    vfs_host_ctx_t *ctx = (vfs_host_ctx_t *)vfs->ctx;
    fs_node_t *nodes = NULL;
    int count = 0;
    if (fs_provider_list(ctx->provider, path, &nodes, &count) != 0)
        return -1;
    size_t off = 0;
    for (int i = 0; i < count && off < names_len; i++) {
        size_t len = strlen(nodes[i].name) + 1;
        if (off + len <= names_len) {
            mem_domain_copy(names + off, nodes[i].name, len);
            off += len;
        }
        if (off >= names_len) break;
    }
    fs_nodes_free(nodes, count);
    if (count_out) *count_out = count;
    return 0;
}

static void host_destroy(vfs_t *vfs) {
    vfs_host_ctx_t *ctx = (vfs_host_ctx_t *)vfs->ctx;
    if (ctx->provider) fs_provider_destroy(ctx->provider);
    mem_domain_free(MEM_DOMAIN_FS, ctx);
}

static const vfs_ops_t host_ops = {
    host_open, host_read, host_write, host_close, host_list, host_destroy
};

vfs_t *vfs_host_create(void *fs_provider) {
    vfs_host_ctx_t *ctx = mem_domain_calloc(MEM_DOMAIN_FS, 1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->base.ops = &host_ops;
    ctx->base.ctx = ctx;
    ctx->provider = (fs_provider_t *)fs_provider;
    return &ctx->base;
}

/* Memory VFS - simple in-memory key-value */
#define MEM_VFS_MAX_ENTRIES 64
typedef struct {
    char key[128];
    char *val;
    size_t val_len;
} mem_vfs_entry_t;

typedef struct {
    vfs_t base;
    mem_vfs_entry_t entries[MEM_VFS_MAX_ENTRIES];
    int count;
} vfs_memory_ctx_t;

static int mem_open(vfs_t *vfs, const char *path, void **handle_out) {
    (void)vfs;
    if (!path || !handle_out) return -1;
    *handle_out = (void *)path;
    return 0;
}

static int mem_read(vfs_t *vfs, void *handle, void *buf, size_t size, size_t *read_out) {
    vfs_memory_ctx_t *ctx = (vfs_memory_ctx_t *)vfs->ctx;
    const char *path = (const char *)handle;
    for (int i = 0; i < ctx->count; i++) {
        if (strcmp(ctx->entries[i].key, path) == 0 && ctx->entries[i].val) {
            size_t n = ctx->entries[i].val_len;
            if (n > size) n = size;
            mem_domain_copy(buf, ctx->entries[i].val, n);
            if (read_out) *read_out = n;
            return 0;
        }
    }
    return -1;
}

static int mem_write(vfs_t *vfs, void *handle, const void *buf, size_t size, size_t *written_out) {
    vfs_memory_ctx_t *ctx = (vfs_memory_ctx_t *)vfs->ctx;
    const char *path = (const char *)handle;
    for (int i = 0; i < ctx->count; i++) {
        if (strcmp(ctx->entries[i].key, path) == 0) {
            if (ctx->entries[i].val) mem_domain_free(MEM_DOMAIN_FS, ctx->entries[i].val);
            ctx->entries[i].val = mem_domain_alloc(MEM_DOMAIN_FS, size + 1);
            if (!ctx->entries[i].val) return -1;
            mem_domain_copy(ctx->entries[i].val, buf, size);
            ((char *)ctx->entries[i].val)[size] = '\0';
            ctx->entries[i].val_len = size;
            if (written_out) *written_out = size;
            return 0;
        }
    }
    if (ctx->count >= MEM_VFS_MAX_ENTRIES) return -1;
    strncpy(ctx->entries[ctx->count].key, path, sizeof(ctx->entries[0].key) - 1);
    ctx->entries[ctx->count].val = mem_domain_alloc(MEM_DOMAIN_FS, size + 1);
    if (!ctx->entries[ctx->count].val) return -1;
    mem_domain_copy(ctx->entries[ctx->count].val, buf, size);
    ((char *)ctx->entries[ctx->count].val)[size] = '\0';
    ctx->entries[ctx->count].val_len = size;
    ctx->count++;
    if (written_out) *written_out = size;
    return 0;
}

static int mem_close(vfs_t *vfs, void *handle) {
    (void)vfs;
    (void)handle;
    return 0;
}

static int mem_list(vfs_t *vfs, const char *path, char *names, size_t names_len, int *count_out) {
    vfs_memory_ctx_t *ctx = (vfs_memory_ctx_t *)vfs->ctx;
    (void)path;
    size_t off = 0;
    for (int i = 0; i < ctx->count && off < names_len; i++) {
        size_t len = strlen(ctx->entries[i].key) + 1;
        if (off + len <= names_len) {
            mem_domain_copy(names + off, ctx->entries[i].key, len);
            off += len;
        }
    }
    if (count_out) *count_out = ctx->count;
    return 0;
}

static void mem_destroy(vfs_t *vfs) {
    vfs_memory_ctx_t *ctx = (vfs_memory_ctx_t *)vfs->ctx;
    for (int i = 0; i < ctx->count; i++)
        if (ctx->entries[i].val) mem_domain_free(MEM_DOMAIN_FS, ctx->entries[i].val);
    mem_domain_free(MEM_DOMAIN_FS, ctx);
}

static const vfs_ops_t mem_ops = {
    mem_open, mem_read, mem_write, mem_close, mem_list, mem_destroy
};

vfs_t *vfs_memory_create(void) {
    vfs_memory_ctx_t *ctx = mem_domain_calloc(MEM_DOMAIN_FS, 1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->base.ops = &mem_ops;
    ctx->base.ctx = ctx;
    mem_domain_zero(ctx->entries, sizeof(ctx->entries));
    return &ctx->base;
}

void vfs_destroy(vfs_t *vfs) {
    if (vfs && vfs->ops && vfs->ops->destroy)
        vfs->ops->destroy(vfs);
}
