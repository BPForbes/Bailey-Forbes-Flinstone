#include "fs_facade.h"
#include "path_log.h"
#include "../identity/session.h"
#include "../identity/path_property.h"
#include "fl/authz_subsystem.h"
#include "contract_p2_authz.h"
#include "server_shared_fs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int check_policy_delete(const char *path, const char *arg2, void *ctx);
static int check_policy_write(const char *path, const char *arg2, void *ctx);

file_manager_service_t *fm_service_create(fs_provider_t *provider) {
    file_manager_service_t *svc = calloc(1, sizeof(*svc));
    if (!svc) return NULL;
    pthread_mutex_init(&svc->undo_mutex, NULL);
    svc->provider = provider;
    fl_session_init();
    strncpy(svc->current_user, fl_session_current_user(), FS_SESSION_USER_MAX - 1);
    svc->current_user[FS_SESSION_USER_MAX - 1] = '\0';
    fs_chain_init(&svc->delete_chain);
    fs_chain_init(&svc->write_chain);
    fs_chain_add(&svc->delete_chain, check_policy_delete);
    fs_chain_add(&svc->write_chain, check_policy_write);
    return svc;
}

void fm_service_set_user(file_manager_service_t *svc, const char *user) {
    if (!svc || !user) return;
    strncpy(svc->current_user, user, FS_SESSION_USER_MAX - 1);
    svc->current_user[FS_SESSION_USER_MAX - 1] = '\0';
}

void fm_service_destroy(file_manager_service_t *svc) {
    fs_chain_clear(&svc->delete_chain);
    fs_chain_clear(&svc->write_chain);
    pthread_mutex_lock(&svc->undo_mutex);
    for (int i = 0; i < svc->undo_top; i++)
        fs_cmd_destroy(svc->undo_stack[i]);
    svc->undo_top = 0;
    pthread_mutex_unlock(&svc->undo_mutex);
    pthread_mutex_destroy(&svc->undo_mutex);
    free(svc);
}

void fm_service_set_policy(file_manager_service_t *svc, fs_access_policy_t *policy) {
    svc->policy = policy;
}

static int fm_authz_path_gate(const char *path) {
    fl_path_property_t prop;
    if (fl_authz_subsystem_check(FL_AUTHZ_OP_VFS_PRIVILEGED, (void *)(uintptr_t)path)
        == FL_AUTHZ_DENY)
        return -1;
    if (fl_path_property_resolve(path, &prop) == FL_RESULT_OK && prop.requires_elevation) {
        if (!fl_session_has_elevation())
            return -1;
        if (prop.owner[0] && strcmp(prop.owner, fl_session_current_user()) != 0
            && !fl_session_is_elevated_account())
            return -1;
    }
    return 0;
}

static int check_policy_delete(const char *path, const char *arg2, void *ctx) {
    (void)arg2;
    file_manager_service_t *svc = (file_manager_service_t *)ctx;
    if (fm_authz_path_gate(path) != 0)
        return -1;
    if (!svc->policy || !svc->policy->vtable) return 0;
    if (!svc->policy->vtable->can_delete(svc->policy, svc->current_user, path))
        return -1;  /* denied */
    return 0;
}

static int check_policy_write(const char *path, const char *arg2, void *ctx) {
    (void)arg2;
    file_manager_service_t *svc = (file_manager_service_t *)ctx;
    if (fm_authz_path_gate(path) != 0)
        return -1;
    if (!svc->policy || !svc->policy->vtable) return 0;
    if (!svc->policy->vtable->can_write(svc->policy, svc->current_user, path))
        return -1;
    return 0;
}

static void push_undo(file_manager_service_t *svc, fs_command_t *cmd) {
    pthread_mutex_lock(&svc->undo_mutex);
    if (svc->undo_top < FS_UNDO_STACK_MAX && cmd) {
        svc->undo_stack[svc->undo_top++] = cmd;
    } else if (cmd) {
        fs_cmd_destroy(cmd);
    }
    pthread_mutex_unlock(&svc->undo_mutex);
}

int fm_list(file_manager_service_t *svc, const char *path, fs_node_t **out, int *count) {
    return fs_provider_list(svc->provider, path, out, count);
}

int fm_read_text(file_manager_service_t *svc, const char *path, char *buf, size_t bufsiz) {
    return fs_provider_read_text(svc->provider, path, buf, bufsiz);
}

int fm_save_text(file_manager_service_t *svc, const char *path, const char *content) {
    if (fs_chain_run(&svc->write_chain, path, content, svc) != 0)
        return -1;
    fs_command_t *cmd = fs_cmd_write_text(svc->provider, path, content);
    if (!cmd) return -1;
    int r = fs_cmd_execute(cmd);
    if (r == 0) {
        push_undo(svc, cmd);
        fs_event_t ev;
        ev.type = FS_EV_FILE_SAVED;
        ev.path[0] = '\0';
        strncpy(ev.path, path, sizeof(ev.path) - 1);
        fs_events_publish(&ev);
    } else {
        fs_cmd_destroy(cmd);
    }
    return r;
}

int fm_read_bytes(file_manager_service_t *svc, const char *path, size_t max, uint8_t **data, size_t *len) {
    if (data) *data = NULL;
    if (len) *len = 0;
    if (!svc || !path) return -1;
    if (svc->policy && svc->policy->vtable && svc->policy->vtable->can_read
        && !svc->policy->vtable->can_read(svc->policy, svc->current_user, path))
        return -1;
    int r = fs_provider_read_bytes(svc->provider, path, max, data, len);
    if (r == 0) path_log_record(PATH_OP_READ, path);
    return r;
}

static void fm_bytes_saved(const char *path) {
    fs_event_t ev;
    ev.type = FS_EV_FILE_SAVED;
    ev.path[0] = '\0';
    strncpy(ev.path, path, sizeof(ev.path) - 1);
    fs_events_publish(&ev);
    path_log_record(PATH_OP_WRITE, path);
}

int fm_write_bytes(file_manager_service_t *svc, const char *path, const uint8_t *data, size_t len) {
    if (!svc || !path) return -1;
    if (fs_chain_run(&svc->write_chain, path, NULL, svc) != 0)
        return -1;
    int r = fs_provider_write_bytes(svc->provider, path, data, len);
    if (r == 0) fm_bytes_saved(path);
    return r;
}

int fm_write_atomic(file_manager_service_t *svc, const char *path, const uint8_t *data, size_t len) {
    if (!svc || !path) return -1;
    if (fs_chain_run(&svc->write_chain, path, NULL, svc) != 0)
        return -1;
    int r = fs_provider_write_atomic(svc->provider, path, data, len);
    if (r == 0) fm_bytes_saved(path);
    return r;
}

/* The directory's path and a name, for the policy and the logs only. */
static int dir_display(const fs_dir_ref_t *d, const char *name, char *out, size_t cap) {
    if (!d || !name) return -1;
    return fl_fs_join_path(out, cap, d->dir ? d->dir : "", name) == 0 ? 0 : -1;
}

static int can_read_path(file_manager_service_t *svc, const char *path) {
    if (svc->policy && svc->policy->vtable && svc->policy->vtable->can_read
        && !svc->policy->vtable->can_read(svc->policy, svc->current_user, path))
        return 0;
    return 1;
}

int fm_dir_open(file_manager_service_t *svc, const char *dir, fs_dir_ref_t *out) {
    if (out) { out->fd = -1; out->dir = NULL; }
    if (!svc || !dir || !out) return -1;
    if (!can_read_path(svc, dir)) return -1;
    return fs_provider_dir_open(svc->provider, dir, out);
}

void fm_dir_close(file_manager_service_t *svc, fs_dir_ref_t *d) {
    if (svc) fs_provider_dir_close(svc->provider, d);
}

int fm_dir_read(file_manager_service_t *svc, const fs_dir_ref_t *d, const char *name, size_t max,
                uint8_t **data, size_t *len) {
    char shown[FL_FS_PATH_MAX];
    int r;

    if (data) *data = NULL;
    if (len) *len = 0;
    if (!svc || dir_display(d, name, shown, sizeof shown) != 0) return -1;
    if (!can_read_path(svc, shown)) return -1;
    r = fs_provider_dir_read(svc->provider, d, name, max, data, len);
    if (r == 0) path_log_record(PATH_OP_READ, shown);
    return r;
}

int fm_dir_exists(file_manager_service_t *svc, const fs_dir_ref_t *d, const char *name) {
    char shown[FL_FS_PATH_MAX];

    if (!svc || dir_display(d, name, shown, sizeof shown) != 0) return -1;
    if (!can_read_path(svc, shown)) return -1;
    return fs_provider_dir_exists(svc->provider, d, name);
}

int fm_dir_read_tail(file_manager_service_t *svc, const fs_dir_ref_t *d, const char *name,
                     size_t unit, size_t keep, uint8_t **data, size_t *len) {
    char shown[FL_FS_PATH_MAX];
    int r;

    if (data) *data = NULL;
    if (len) *len = 0;
    if (!svc || dir_display(d, name, shown, sizeof shown) != 0) return -1;
    if (!can_read_path(svc, shown)) return -1;
    r = fs_provider_dir_read_tail(svc->provider, d, name, unit, keep, data, len);
    if (r == 0) path_log_record(PATH_OP_READ, shown);
    return r;
}

int fm_dir_commit(file_manager_service_t *svc, const fs_dir_ref_t *d, const fs_dir_file_t *files,
                  size_t n) {
    char shown[4][FL_FS_PATH_MAX];
    size_t i;
    int r;

    if (!svc || !d || !files || n == 0 || n > 4u) return -1;
    /* Every member is authorised before any is staged: one denial leaves all of them alone. */
    for (i = 0; i < n; i++) {
        if (dir_display(d, files[i].name, shown[i], sizeof shown[i]) != 0) return -1;
        if (fs_chain_run(&svc->write_chain, shown[i], NULL, svc) != 0) return -1;
    }
    r = fs_provider_dir_commit(svc->provider, d, files, n);
    if (r == 0)
        for (i = 0; i < n; i++) fm_bytes_saved(shown[i]);
    return r;
}

int fm_create_file(file_manager_service_t *svc, const char *path) {
    if (fs_chain_run(&svc->write_chain, path, NULL, svc) != 0)
        return -1;
    fs_command_t *cmd = fs_cmd_create_file(svc->provider, path);
    if (!cmd) return -1;
    int r = fs_cmd_execute(cmd);
    if (r == 0) {
        push_undo(svc, cmd);
        fs_event_t ev = { FS_EV_FILE_CREATED };
        strncpy(ev.path, path, sizeof(ev.path) - 1);
        fs_events_publish(&ev);
    } else {
        fs_cmd_destroy(cmd);
    }
    return r;
}

int fm_create_dir(file_manager_service_t *svc, const char *path) {
    if (fs_chain_run(&svc->write_chain, path, NULL, svc) != 0)
        return -1;
    fs_command_t *cmd = fs_cmd_create_dir(svc->provider, path);
    if (!cmd) return -1;
    int r = fs_cmd_execute(cmd);
    if (r == 0) {
        push_undo(svc, cmd);
        fs_event_t ev;
        ev.type = FS_EV_DIR_CREATED;
        ev.path[0] = '\0';
        strncpy(ev.path, path, sizeof(ev.path) - 1);
        fs_events_publish(&ev);
    } else {
        fs_cmd_destroy(cmd);
    }
    return r;
}

/* Delete is non-undoable: we do not push to undo stack */
int fm_delete(file_manager_service_t *svc, const char *path) {
    if (fs_chain_run(&svc->delete_chain, path, NULL, svc) != 0)
        return -1;
    fs_command_t *cmd = fs_cmd_delete(svc->provider, path);
    if (!cmd) return -1;
    int r = fs_cmd_execute(cmd);
    fs_cmd_destroy(cmd);  /* Delete: no undo */
    if (r == 0) {
        fs_event_t ev;
        ev.type = FS_EV_FILE_DELETED;
        ev.path[0] = '\0';
        strncpy(ev.path, path, sizeof(ev.path) - 1);
        fs_events_publish(&ev);
    }
    return r;
}

int fm_move(file_manager_service_t *svc, const char *src, const char *dst) {
    if (fs_chain_run(&svc->write_chain, src, NULL, svc) != 0)
        return -1;
    if (fs_chain_run(&svc->write_chain, dst, NULL, svc) != 0)
        return -1;
    fs_command_t *cmd = fs_cmd_move(svc->provider, src, dst);
    if (!cmd) return -1;
    int r = fs_cmd_execute(cmd);
    if (r == 0)
        push_undo(svc, cmd);
    else
        fs_cmd_destroy(cmd);
    return r;
}

int fm_undo(file_manager_service_t *svc) {
    pthread_mutex_lock(&svc->undo_mutex);
    if (svc->undo_top == 0) {
        pthread_mutex_unlock(&svc->undo_mutex);
        return -1;
    }
    fs_command_t *cmd = svc->undo_stack[--svc->undo_top];
    pthread_mutex_unlock(&svc->undo_mutex);
    int r = fs_cmd_undo(cmd);
    fs_cmd_destroy(cmd);
    return r;
}

int fm_undo_available(file_manager_service_t *svc) {
    pthread_mutex_lock(&svc->undo_mutex);
    int n = svc->undo_top;
    pthread_mutex_unlock(&svc->undo_mutex);
    return n;
}
