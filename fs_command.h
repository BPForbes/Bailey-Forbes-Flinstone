#ifndef FS_COMMAND_H
#define FS_COMMAND_H

#include "fs_types.h"

/* Create undoable commands */
fs_command_t *fs_cmd_create_file(fs_provider_t *provider, const char *path);
fs_command_t *fs_cmd_create_dir(fs_provider_t *provider, const char *path);
fs_command_t *fs_cmd_delete(fs_provider_t *provider, const char *path);
fs_command_t *fs_cmd_move(fs_provider_t *provider, const char *src, const char *dst);
fs_command_t *fs_cmd_write_text(fs_provider_t *provider, const char *path, const char *content);

int fs_cmd_execute(fs_command_t *cmd);
int fs_cmd_undo(fs_command_t *cmd);
void fs_cmd_destroy(fs_command_t *cmd);

#endif /* FS_COMMAND_H */
