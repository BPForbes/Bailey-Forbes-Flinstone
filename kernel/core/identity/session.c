#include "session.h"
#include "elevation.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static fl_user_db_t s_db;
static char s_current[FL_USER_NAME_MAX];
static fl_elevation_token_t s_active_elev = FL_ELEVATION_TOKEN_NONE;
static int s_sudo_scope_depth;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void session_load_once(void) {
    const char *path = getenv("FL_USERS_DB_PATH");
    if (!path || !path[0])
        path = FL_SESSION_USERS_PATH;
    if (fl_user_db_load(&s_db, path) != FL_RESULT_OK) {
        fl_user_db_seed_defaults(&s_db);
        strncpy(s_db.path, path, sizeof(s_db.path) - 1);
        (void)fl_user_db_save(&s_db);
    }
    strncpy(s_current, s_db.default_user, sizeof(s_current) - 1);
    s_current[sizeof(s_current) - 1] = '\0';
}

void fl_session_init(void) {
    pthread_once(&s_once, session_load_once);
}

const char *fl_session_current_user(void) {
    fl_session_init();
    return s_current;
}

int fl_session_is_elevated_account(void) {
    fl_session_init();
    return fl_user_db_is_elevated_user(&s_db, s_current);
}

int fl_session_in_sudo_scope(void) {
    fl_session_init();
    return s_sudo_scope_depth > 0;
}

void fl_session_begin_sudo_scope(void) {
    fl_session_init();
    s_sudo_scope_depth++;
}

void fl_session_end_sudo_scope(void) {
    fl_session_init();
    if (s_sudo_scope_depth > 0)
        s_sudo_scope_depth--;
}

int fl_session_jail_privileged(void) {
    fl_session_init();
    if (fl_session_is_elevated_account())
        return 1;
    return s_sudo_scope_depth > 0;
}

int fl_session_has_elevation(void) {
    fl_session_init();
    if (fl_session_is_elevated_account())
        return 1;
    if (s_sudo_scope_depth > 0)
        return 1;
    return fl_elevation_active(s_active_elev);
}

fl_result_t fl_session_login(const char *name, const char *password) {
    fl_session_init();
    if (!name || !name[0] || !password)
        return FL_RESULT_INVAL;
    if (!fl_user_db_verify_password(&s_db, name, password))
        return FL_RESULT_ACCES;
    strncpy(s_current, name, sizeof(s_current) - 1);
    s_current[sizeof(s_current) - 1] = '\0';
    s_active_elev = FL_ELEVATION_TOKEN_NONE;
    return FL_RESULT_OK;
}

fl_result_t fl_session_set_user(const char *name) {
    const fl_user_record_t *u;
    fl_session_init();
    if (!name || !name[0])
        return FL_RESULT_INVAL;
    u = fl_user_db_find(&s_db, name);
    if (!u)
        return FL_RESULT_NOENT;
    strncpy(s_current, name, sizeof(s_current) - 1);
    s_current[sizeof(s_current) - 1] = '\0';
    s_active_elev = FL_ELEVATION_TOKEN_NONE;
    return FL_RESULT_OK;
}

fl_result_t fl_session_grant_elevation(const char *reason, fl_elevation_token_t *out) {
    fl_result_t rc;
    fl_session_init();
    if (!out)
        return FL_RESULT_INVAL;
    if (fl_session_is_elevated_account()) {
        *out = FL_ELEVATION_TOKEN_NONE;
        return FL_RESULT_OK;
    }
    rc = fl_elevation_grant(s_current, reason, out);
    if (rc == FL_RESULT_OK)
        s_active_elev = *out;
    return rc;
}

void fl_session_bind_elevation(fl_elevation_token_t token) {
    fl_session_init();
    s_active_elev = token;
}

void fl_session_clear_elevation(void) {
    fl_session_init();
    s_active_elev = FL_ELEVATION_TOKEN_NONE;
}

fl_user_db_t *fl_session_user_db_mut(void) {
    fl_session_init();
    return &s_db;
}

const fl_user_db_t *fl_session_user_db(void) {
    fl_session_init();
    return &s_db;
}

fl_result_t fl_session_save_users(void) {
    fl_session_init();
    return fl_user_db_save(&s_db);
}
