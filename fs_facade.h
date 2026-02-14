#ifndef FS_FACADE_H
#define FS_FACADE_H

#include "fs_types.h"
#include "fs_provider.h"
#include "fs_command.h"
#include "fs_events.h"
#include "fs_policy.h"
#include "fs_chain.h"

#define FS_UNDO_STACK_MAX 32

/* FileManagerService - Facade for UI; never touches filesystem directly */
typedef struct file_manager_service {
    fs_provider_t *provider;
    fs_access_policy_t *policy;
    fs_validation_chain_t delete_chain;
    fs_validation_chain_t write_chain;
    fs_command_t *undo_stack[FS_UNDO_STACK_MAX];
    int undo_top;
    int ui_refresh_pending;
} file_manager_service_t;

file_manager_service_t *fm_service_create(fs_provider_t *provider);
void fm_service_destroy(file_manager_service_t *svc);
void fm_service_set_policy(file_manager_service_t *svc, fs_access_policy_t *policy);

int fm_list(file_manager_service_t *svc, const char *path, fs_node_t **out, int *count);
int fm_read_text(file_manager_service_t *svc, const char *path, char *buf, size_t bufsiz);
int fm_save_text(file_manager_service_t *svc, const char *path, const char *content);
int fm_create_file(file_manager_service_t *svc, const char *path);
int fm_create_dir(file_manager_service_t *svc, const char *path);
int fm_delete(file_manager_service_t *svc, const char *path);
int fm_move(file_manager_service_t *svc, const char *src, const char *dst);

int fm_undo(file_manager_service_t *svc);
int fm_undo_available(file_manager_service_t *svc);

#endif /* FS_FACADE_H */
