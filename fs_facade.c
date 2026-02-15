#include "fs_facade.h"
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
    strncpy(svc->current_user, "user", FS_SESSION_USER_MAX - 1);
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

static int check_policy_delete(const char *path, const char *arg2, void *ctx) {
    (void)arg2;
    file_manager_service_t *svc = (file_manager_service_t *)ctx;
    if (!svc->policy || !svc->policy->vtable) return 0;
    if (!svc->policy->vtable->can_delete(svc->policy, svc->current_user, path))
        return -1;  /* denied */
    return 0;
}

static int check_policy_write(const char *path, const char *arg2, void *ctx) {
    (void)arg2;
    file_manager_service_t *svc = (file_manager_service_t *)ctx;
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

int fm_create_file(file_manager_service_t *svc, const char *path) {
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
