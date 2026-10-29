#ifndef FS_FACADE_H
#define FS_FACADE_H

#include "fs_types.h"
#include "fs_provider.h"
#include "fs_command.h"
#include "fs_events.h"
#include "fs_policy.h"
#include "fs_chain.h"
#include <pthread.h>

#define FS_UNDO_STACK_MAX 32
#define FS_SESSION_USER_MAX 32

/* FileManagerService - Facade for UI; never touches filesystem directly */
typedef struct file_manager_service {
    fs_provider_t *provider;
    fs_access_policy_t *policy;
    char current_user[FS_SESSION_USER_MAX];  /* Session identity for policy checks */
    fs_validation_chain_t delete_chain;
    fs_validation_chain_t write_chain;
    fs_command_t *undo_stack[FS_UNDO_STACK_MAX];
    int undo_top;
    int ui_refresh_pending;
    pthread_mutex_t undo_mutex;
} file_manager_service_t;

file_manager_service_t *fm_service_create(fs_provider_t *provider);
void fm_service_destroy(file_manager_service_t *svc);
void fm_service_set_policy(file_manager_service_t *svc, fs_access_policy_t *policy);
void fm_service_set_user(file_manager_service_t *svc, const char *user);

int fm_list(file_manager_service_t *svc, const char *path, fs_node_t **out, int *count);
int fm_read_text(file_manager_service_t *svc, const char *path, char *buf, size_t bufsiz);
int fm_save_text(file_manager_service_t *svc, const char *path, const char *content);
/* Byte-exact calls: fm_read_bytes asks the policy's can_read, the two writes run write_chain
 * like fm_save_text, publish FS_EV_FILE_SAVED and record path_log. No undo (the undo command
 * snapshots text only). Returns follow fs_provider_read_bytes; a denial is -1. */
int fm_read_bytes(file_manager_service_t *svc, const char *path, size_t max, uint8_t **data, size_t *len);
int fm_write_bytes(file_manager_service_t *svc, const char *path, const uint8_t *data, size_t len);
int fm_write_atomic(file_manager_service_t *svc, const char *path, const uint8_t *data, size_t len);
/*
 * Contained calls over a held directory (fs_dir_ref_t), the facade's side of the provider's
 * dir slots. The policy, the write chain, the events and the path log see the directory's path
 * joined with the name, for display only: the I/O never reopens that path. fm_dir_open and the
 * reads ask can_read; fm_dir_commit runs write_chain for EVERY file before the provider stages
 * any of them (one denial leaves all untouched), and publishes FS_EV_FILE_SAVED and records
 * path_log for each file only after the whole commit succeeded. A denial is -1.
 */
int fm_dir_open(file_manager_service_t *svc, const char *dir, fs_dir_ref_t *out);
void fm_dir_close(file_manager_service_t *svc, fs_dir_ref_t *d);
int fm_dir_read(file_manager_service_t *svc, const fs_dir_ref_t *d, const char *name, size_t max,
                uint8_t **data, size_t *len);
int fm_dir_exists(file_manager_service_t *svc, const fs_dir_ref_t *d, const char *name);
int fm_dir_read_tail(file_manager_service_t *svc, const fs_dir_ref_t *d, const char *name,
                     size_t unit, size_t keep, uint8_t **data, size_t *len);
int fm_dir_commit(file_manager_service_t *svc, const fs_dir_ref_t *d, const fs_dir_file_t *files,
                  size_t n);
int fm_create_file(file_manager_service_t *svc, const char *path);
int fm_create_dir(file_manager_service_t *svc, const char *path);
int fm_delete(file_manager_service_t *svc, const char *path);
int fm_move(file_manager_service_t *svc, const char *src, const char *dst);

int fm_undo(file_manager_service_t *svc);
int fm_undo_available(file_manager_service_t *svc);

#endif /* FS_FACADE_H */
