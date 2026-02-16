#include "fs_command.h"
#include "fs_provider.h"
#include <stdlib.h>
#include <string.h>

#define CMD_STR_MAX 4096

typedef struct {
    fs_command_t base;
    fs_provider_t *provider;
    char path[FS_PATH_MAX];
    char path2[FS_PATH_MAX];  /* for move: dst */
    char *content;            /* for write: backup on undo */
    size_t content_len;
} cmd_impl_t;

static int cmd_create_file_exec(fs_command_t *cmd) {
    cmd_impl_t *c = (cmd_impl_t *)cmd;
    return fs_provider_create_file(c->provider, c->path);
}

static int cmd_create_file_undo(fs_command_t *cmd) {
    cmd_impl_t *c = (cmd_impl_t *)cmd;
    return fs_provider_delete(c->provider, c->path);
}

static void cmd_create_file_destroy(fs_command_t *cmd) {
    free(cmd);
}

static const fs_command_vtable_t create_file_vtable = {
    cmd_create_file_exec, cmd_create_file_undo, cmd_create_file_destroy
};

fs_command_t *fs_cmd_create_file(fs_provider_t *provider, const char *path) {
    cmd_impl_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->base.vtable = &create_file_vtable;
    c->provider = provider;
    strncpy(c->path, path, FS_PATH_MAX - 1);
    return &c->base;
}

/* Create dir */
static int cmd_create_dir_exec(fs_command_t *cmd) {
    cmd_impl_t *c = (cmd_impl_t *)cmd;
    return fs_provider_create_dir(c->provider, c->path);
}

static int cmd_create_dir_undo(fs_command_t *cmd) {
    cmd_impl_t *c = (cmd_impl_t *)cmd;
    return fs_provider_delete(c->provider, c->path);
}

static const fs_command_vtable_t create_dir_vtable = {
    cmd_create_dir_exec, cmd_create_dir_undo, cmd_create_file_destroy
};

fs_command_t *fs_cmd_create_dir(fs_provider_t *provider, const char *path) {
    cmd_impl_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->base.vtable = &create_dir_vtable;
    c->provider = provider;
    strncpy(c->path, path, FS_PATH_MAX - 1);
    return &c->base;
}

/* Delete - undo restores by... we'd need to save content. Simplified: delete stores path for undo message only */
static int cmd_delete_exec(fs_command_t *cmd) {
    cmd_impl_t *c = (cmd_impl_t *)cmd;
    return fs_provider_delete(c->provider, c->path);
}

static int cmd_delete_undo(fs_command_t *cmd) {
    (void)cmd;
    /* Full undo would require saving file content before delete - stub */
    return -1;
}

static const fs_command_vtable_t delete_vtable = {
    cmd_delete_exec, cmd_delete_undo, cmd_create_file_destroy
};

fs_command_t *fs_cmd_delete(fs_provider_t *provider, const char *path) {
    cmd_impl_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->base.vtable = &delete_vtable;
    c->provider = provider;
    strncpy(c->path, path, FS_PATH_MAX - 1);
    return &c->base;
}

/* Move */
static int cmd_move_exec(fs_command_t *cmd) {
    cmd_impl_t *c = (cmd_impl_t *)cmd;
    return fs_provider_move(c->provider, c->path, c->path2);
}

static int cmd_move_undo(fs_command_t *cmd) {
    cmd_impl_t *c = (cmd_impl_t *)cmd;
    return fs_provider_move(c->provider, c->path2, c->path);
}

static const fs_command_vtable_t move_vtable = {
    cmd_move_exec, cmd_move_undo, cmd_create_file_destroy
};

fs_command_t *fs_cmd_move(fs_provider_t *provider, const char *src, const char *dst) {
    cmd_impl_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->base.vtable = &move_vtable;
    c->provider = provider;
    strncpy(c->path, src, FS_PATH_MAX - 1);
    strncpy(c->path2, dst, FS_PATH_MAX - 1);
    return &c->base;
}

/* Write text - undo restores previous content */
typedef struct {
    fs_command_t base;
    fs_provider_t *provider;
    char path[FS_PATH_MAX];
    char *new_content;
    char *old_content;
} cmd_write_impl_t;

static int cmd_write_exec(fs_command_t *cmd) {
    cmd_write_impl_t *c = (cmd_write_impl_t *)cmd;
    char prev[CMD_STR_MAX];
    int n = fs_provider_read_text(c->provider, c->path, prev, sizeof(prev));
    if (n >= 0) {
        c->old_content = strdup(prev);
    }
    return fs_provider_write_text(c->provider, c->path, c->new_content);
}

static int cmd_write_undo(fs_command_t *cmd) {
    cmd_write_impl_t *c = (cmd_write_impl_t *)cmd;
    if (!c->old_content) return -1;
    int r = fs_provider_write_text(c->provider, c->path, c->old_content);
    free(c->old_content);
    c->old_content = NULL;
    return r;
}

static void cmd_write_destroy(fs_command_t *cmd) {
    cmd_write_impl_t *c = (cmd_write_impl_t *)cmd;
    free(c->new_content);
    free(c->old_content);
    free(cmd);
}

static const fs_command_vtable_t write_text_vtable = {
    cmd_write_exec, cmd_write_undo, cmd_write_destroy
};

fs_command_t *fs_cmd_write_text(fs_provider_t *provider, const char *path, const char *content) {
    cmd_write_impl_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->base.vtable = &write_text_vtable;
    c->provider = provider;
    strncpy(c->path, path, FS_PATH_MAX - 1);
    c->new_content = content ? strdup(content) : strdup("");
    return &c->base;
}

int fs_cmd_execute(fs_command_t *cmd) {
    return cmd->vtable->execute(cmd);
}

int fs_cmd_undo(fs_command_t *cmd) {
    return cmd->vtable->undo(cmd);
}

void fs_cmd_destroy(fs_command_t *cmd) {
    cmd->vtable->destroy(cmd);
}
