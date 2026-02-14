#ifndef FS_TYPES_H
#define FS_TYPES_H

#include <stddef.h>

#define FS_PATH_MAX 256
#define FS_NAME_MAX 64

/* ---------------------------------------------------------------------------
 * Composite: INode - files and directories modeled uniformly
 * --------------------------------------------------------------------------- */
typedef enum { NODE_FILE, NODE_DIR } node_type_t;

typedef struct fs_node fs_node_t;
struct fs_node {
    node_type_t type;
    char name[FS_NAME_MAX];
    char path[FS_PATH_MAX];
    size_t size;
    fs_node_t *parent;
    fs_node_t *children;      /* first child (for dirs) */
    fs_node_t *sibling;       /* next sibling */
    void *provider_data;      /* backend-specific */
};

/* FileEntry / DirectoryEntry - thin wrappers for API */
typedef fs_node_t file_entry_t;
typedef fs_node_t directory_entry_t;

/* ---------------------------------------------------------------------------
 * Strategy: IFileSystemProvider
 * --------------------------------------------------------------------------- */
typedef struct fs_provider fs_provider_t;

typedef struct fs_provider_vtable {
    int (*list_entries)(fs_provider_t *p, const char *path, fs_node_t **out, int *count);
    int (*read_text)(fs_provider_t *p, const char *path, char *buf, size_t bufsiz);
    int (*write_text)(fs_provider_t *p, const char *path, const char *content);
    int (*create_file)(fs_provider_t *p, const char *path);
    int (*create_dir)(fs_provider_t *p, const char *path);
    int (*delete_path)(fs_provider_t *p, const char *path);
    int (*move_path)(fs_provider_t *p, const char *src, const char *dst);
    int (*get_metadata)(fs_provider_t *p, const char *path, fs_node_t *out);
    void (*destroy)(fs_provider_t *p);
} fs_provider_vtable_t;

struct fs_provider {
    const fs_provider_vtable_t *vtable;
    void *impl;
};

/* ---------------------------------------------------------------------------
 * Command: ICommand - undoable operations
 * --------------------------------------------------------------------------- */
typedef struct fs_command fs_command_t;

typedef struct fs_command_vtable {
    int (*execute)(fs_command_t *cmd);
    int (*undo)(fs_command_t *cmd);
    void (*destroy)(fs_command_t *cmd);
} fs_command_vtable_t;

struct fs_command {
    const fs_command_vtable_t *vtable;
    void *impl;
};

/* ---------------------------------------------------------------------------
 * Policy: IAccessPolicy
 * --------------------------------------------------------------------------- */
typedef struct fs_access_policy fs_access_policy_t;

typedef struct fs_access_policy_vtable {
    int (*can_read)(fs_access_policy_t *p, const char *user, const char *path);
    int (*can_write)(fs_access_policy_t *p, const char *user, const char *path);
    int (*can_delete)(fs_access_policy_t *p, const char *user, const char *path);
} fs_access_policy_vtable_t;

struct fs_access_policy {
    const fs_access_policy_vtable_t *vtable;
    void *impl;
};

#endif /* FS_TYPES_H */
