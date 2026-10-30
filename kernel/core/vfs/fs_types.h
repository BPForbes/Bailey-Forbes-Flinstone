#ifndef FS_TYPES_H
#define FS_TYPES_H

#include <stddef.h>
#include <stdint.h>

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

/*
 * A held landing directory, for the contained slots at the end of the vtable below: the fd is
 * opened once (fl_fs_dir_open) and every name is then used inside it, never joined to a path,
 * so a link planted at a name is not followed. dir is the directory's own path, for display,
 * policy and path logs only; no slot opens it again. fd is -1 only where the platform has no
 * *at calls at all (fl_fs_contained_available() is 0).
 */
typedef struct fs_dir_ref {
    int         fd;
    const char *dir;
} fs_dir_ref_t;

/* One file of fs_provider_dir_commit: a plain name inside the held directory and its bytes. */
typedef struct fs_dir_file {
    const char    *name;
    const uint8_t *data;
    size_t         len;
} fs_dir_file_t;

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
    /*
     * Byte-exact slots, appended AFTER destroy: both vtables are positional initialisers, so
     * a new slot goes at the end only. A provider that predates them leaves them NULL and the
     * fs_provider_* wrappers answer -1 (errno ENOSYS) for a NULL slot.
     *
     * Return codes follow fl_fs_read_file: 0, -1 (failure, or denied by the jail), -2 (larger
     * than max), -3 (could not be opened). read_bytes hands back MEM_DOMAIN_FS memory the
     * caller wipes and releases with mem_domain_free; *data is NULL and *len 0 on failure.
     *
     * The three slots below are path-based. The provider sits ABOVE the placement layer, not
     * beside it. server_shared_fs.c (fl_fs_*) is the one layer that decides how bytes land:
     * staged writes, no-follow reads, the dirfd-relative landing (fl_fs_*_in / *_at) and the
     * all-or-nothing fl_fs_commit_files. The local provider is built on it (fl_fs_read_file,
     * fl_fs_stage_file). That is KeyQuorum's own split: its Storage trait (src/storage.rs) is
     * the placement layer that carries the rules (write_new refuses to replace, rename_new never
     * replaces), its logic modules take a Storage, and the files ABI stores nothing itself. A
     * path argument cannot carry the held directory fd and plain leaf name a landing needs, so
     * the contained slots after write_atomic take an fs_dir_ref_t instead: the KeyQuorum
     * checkpoints (kq_checkpoint.c) reach the provider through them, and through the facade's
     * fm_dir_* for policy, events and path logs. The local provider inherits fl_fs_read_file
     * (follows links) and fl_fs_write_file_atomic (never writes through a .part link, but
     * rename replaces a link at the target) for the path-based slots and claims no more.
     */
    int (*read_bytes)(fs_provider_t *p, const char *path, size_t max, uint8_t **data, size_t *len);
    /* Binary-safe full replace (embedded NUL bytes are kept): stages <path>.part, then renames
     * it onto path only when path is not a symlink (ELOOP). A failure leaves the old file as it
     * was. The target gets a new inode: it keeps the old file's permission bits, but not its
     * owner, and hard links to the old file are not updated. A new file is 0600. */
    int (*write_bytes)(fs_provider_t *p, const char *path, const uint8_t *data, size_t len);
    /* Sibling .part file, then rename: a reader sees the old or the new file, never half. */
    int (*write_atomic)(fs_provider_t *p, const char *path, const uint8_t *data, size_t len);
    /*
     * Contained slots, appended after write_atomic (positional initialisers: end only; a NULL
     * slot makes the fs_provider_dir_* wrappers answer -1, errno ENOSYS).
     *
     * dir_open holds dir (its own path may be a link) in *out; 0, or -1 when it cannot be held
     * where the contained calls exist (never a silent fall back to joined paths). dir_close
     * releases it. dir_read, dir_exists, dir_read_tail and dir_commit take one plain name (no
     * separator, not "." or "..") inside the held directory and refuse a link at it. Returns
     * follow fl_fs_read_file_at, fl_fs_exists_at, fl_fs_read_tail_at and fl_fs_commit_files.
     * dir_commit replaces every file or none, 1 to 4 files.
     */
    int (*dir_open)(fs_provider_t *p, const char *dir, fs_dir_ref_t *out);
    void (*dir_close)(fs_provider_t *p, fs_dir_ref_t *d);
    int (*dir_read)(fs_provider_t *p, const fs_dir_ref_t *d, const char *name, size_t max,
                    uint8_t **data, size_t *len);
    int (*dir_exists)(fs_provider_t *p, const fs_dir_ref_t *d, const char *name);
    int (*dir_read_tail)(fs_provider_t *p, const fs_dir_ref_t *d, const char *name, size_t unit,
                         size_t keep, uint8_t **data, size_t *len);
    int (*dir_commit)(fs_provider_t *p, const fs_dir_ref_t *d, const fs_dir_file_t *files,
                      size_t n);
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
