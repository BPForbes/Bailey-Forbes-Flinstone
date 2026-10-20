/*
 * The native shell's side of net_kq_delivery.c: the host's key authority is the shell's
 * account store, and session frames reach the handlers through the server and client hooks.
 * The browser lab builds net_kq_delivery.c without this file.
 */
#include "net_kq_delivery.h"

static fl_net_kq_users_fn s_users;
static fl_user_db_keydir_t s_dir;

/* Called with the module's lock held (host handlers), so one directory is enough. */
static const fl_kq_keydir_t *user_keys(void) {
    const fl_user_db_t *db = s_users ? s_users() : NULL;

    if (!db)
        return NULL;
    fl_user_db_keydir_init(&s_dir, db);
    return &s_dir.base;
}

fl_result_t fl_net_kq_install(fl_net_kq_users_fn users_fn, const char *store_dir) {
    fl_result_t rc;

    if (!users_fn)
        return FL_RESULT_INVAL;
    rc = fl_net_kq_configure(user_keys, store_dir);
    if (rc != FL_RESULT_OK)
        return rc;
    s_users = users_fn;
    fl_net_server_set_kq_hook(fl_net_kq_host_handle);
    fl_net_client_set_kq_hook(fl_net_kq_client_handle);
    return FL_RESULT_OK;
}
