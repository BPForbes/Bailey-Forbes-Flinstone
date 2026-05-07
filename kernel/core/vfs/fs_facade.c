#include "fs_facade.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef EMSCRIPTEN_SINGLE_THREAD
#define FM_LOCK(s)   ((void)0)
#define FM_UNLOCK(s) ((void)0)
#define FM_MUTEX_INIT(s) ((void)((s)->_undo_sync_placeholder = 0))
#define FM_MUTEX_DESTROY(s) ((void)0)
#else
#define FM_LOCK(s)   pthread_mutex_lock(&(s)->undo_mutex)
#define FM_UNLOCK(s) pthread_mutex_unlock(&(s)->undo_mutex)
#define FM_MUTEX_INIT(s) pthread_mutex_init(&(s)->undo_mutex, NULL)
#define FM_MUTEX_DESTROY(s) pthread_mutex_destroy(&(s)->undo_mutex)
#endif

static int check_policy_delete(const char *path, const char *arg2, void *ctx);
static int check_policy_write(const char *path, const char *arg2, void *ctx);

/**
 * Create and initialize a file manager service that wraps an fs_provider.
 *
 * Initializes the service structure, the undo mutex, sets the provided
 * fs_provider, sets the default current user to "user", initializes the
 * delete and write policy chains, and registers the built-in policy check
 * callbacks.
 *
 * @param provider Underlying filesystem provider to be wrapped (may be NULL).
 * @returns Pointer to a newly allocated and initialized file_manager_service_t,
 *          or NULL if allocation fails.
 */
file_manager_service_t *fm_service_create(fs_provider_t *provider) {
    file_manager_service_t *svc = calloc(1, sizeof(*svc));
    if (!svc) return NULL;
    FM_MUTEX_INIT(svc);
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

/**
 * Destroy a file manager service and release all associated resources.
 *
 * Clears the delete and write policy chains, destroys any commands stored
 * in the undo stack while holding the service mutex, resets the undo stack,
 * destroys the mutex, and frees the service structure.
 *
 * @param svc Service instance to destroy (pointer becomes invalid after call).
 */
void fm_service_destroy(file_manager_service_t *svc) {
    fs_chain_clear(&svc->delete_chain);
    fs_chain_clear(&svc->write_chain);
    FM_LOCK(svc);
    for (int i = 0; i < svc->undo_top; i++)
        fs_cmd_destroy(svc->undo_stack[i]);
    svc->undo_top = 0;
    FM_UNLOCK(svc);
    FM_MUTEX_DESTROY(svc);
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

/**
 * Validate write permission for a filesystem path using the service's access policy.
 *
 * Treats `ctx` as a pointer to `file_manager_service_t`; if no policy or vtable is configured the write is allowed.
 * @param path Path to check.
 * @param arg2 Unused.
 * @param ctx Pointer to a `file_manager_service_t`.
 * @returns `0` if write is allowed, `-1` if the policy denies the write.
 */
static int check_policy_write(const char *path, const char *arg2, void *ctx) {
    (void)arg2;
    file_manager_service_t *svc = (file_manager_service_t *)ctx;
    if (!svc->policy || !svc->policy->vtable) return 0;
    if (!svc->policy->vtable->can_write(svc->policy, svc->current_user, path))
        return -1;
    return 0;
}

/**
 * Pushes a command onto the service undo stack for later reversal.
 *
 * If the undo stack has room and `cmd` is non-NULL, the command is stored and
 * the stack pointer is advanced. If the stack is full and `cmd` is non-NULL,
 * the command is destroyed. The operation is protected by the service mutex
 * macros to be safe in single- and multi-threaded builds.
 *
 * @param svc File manager service whose undo stack will receive the command.
 * @param cmd Command to store for undo; if NULL the function does nothing.
 */
static void push_undo(file_manager_service_t *svc, fs_command_t *cmd) {
    FM_LOCK(svc);
    if (svc->undo_top < FS_UNDO_STACK_MAX && cmd) {
        svc->undo_stack[svc->undo_top++] = cmd;
    } else if (cmd) {
        fs_cmd_destroy(cmd);
    }
    FM_UNLOCK(svc);
}

/**
 * Retrieve directory entries for a path using the service's filesystem provider.
 *
 * @param svc File manager service instance.
 * @param path Filesystem path to list.
 * @param out Pointer that will be set to an array of `fs_node_t` populated by the provider.
 * @param count Pointer that will be set to the number of entries placed in `*out`.
 * @return 0 on success, negative error code on failure.
 */
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

/**
 * Move a file or directory from src to dst and record the operation for undo.
 *
 * If the move completes successfully, the executed move is pushed onto the
 * service's undo stack so it can be undone later. If the move fails the
 * created command is destroyed and not added to the undo stack.
 *
 * @param svc Service instance managing the operation and undo stack.
 * @param src Source path to move.
 * @param dst Destination path for the moved item.
 * @returns `0` if the move succeeded, non-zero error code on failure (returns `-1` if command creation failed).
 */
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

/**
 * Undo the most recent undoable filesystem command recorded by the service.
 *
 * Removes the latest command from the service's undo stack, executes its undo action,
 * and destroys the command object.
 *
 * @param svc Service instance whose undo stack will be used.
 * @returns `-1` if no undo is available; otherwise the result code returned by the undo operation
 *          (`0` on success, non-zero on failure).
 */
int fm_undo(file_manager_service_t *svc) {
    FM_LOCK(svc);
    if (svc->undo_top == 0) {
        FM_UNLOCK(svc);
        return -1;
    }
    fs_command_t *cmd = svc->undo_stack[--svc->undo_top];
    FM_UNLOCK(svc);
    int r = fs_cmd_undo(cmd);
    fs_cmd_destroy(cmd);
    return r;
}

/**
 * Report how many undo operations are currently available.
 *
 * @param svc File manager service instance whose undo stack is queried.
 * @returns The number of undoable commands currently stored (0 if none).
 */
int fm_undo_available(file_manager_service_t *svc) {
    FM_LOCK(svc);
    int n = svc->undo_top;
    FM_UNLOCK(svc);
    return n;
}
