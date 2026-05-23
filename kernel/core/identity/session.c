#include "session.h"
#include "elevation.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static fl_user_db_t s_db;
static char s_current[FL_USER_NAME_MAX];
static fl_elevation_token_t s_active_elev = FL_ELEVATION_TOKEN_NONE;
static int s_sudo_scope_depth;
static int s_db_ready;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t s_session_mu = PTHREAD_MUTEX_INITIALIZER;

static void session_load_once(void) {
    const char *path = getenv("FL_USERS_DB_PATH");

    if (!path || !path[0])
        path = FL_SESSION_USERS_PATH;
    pthread_mutex_lock(&s_session_mu);
    if (fl_user_db_load(&s_db, path) != FL_RESULT_OK) {
        fl_user_db_seed_defaults(&s_db);
        strncpy(s_db.path, path, sizeof(s_db.path) - 1);
        (void)fl_user_db_save(&s_db);
    }
    strncpy(s_current, s_db.default_user, sizeof(s_current) - 1);
    s_current[sizeof(s_current) - 1] = '\0';
    s_db_ready = 1;
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_init(void) {
    pthread_once(&s_once, session_load_once);
}

const char *fl_session_current_user(void) {
    fl_session_init();
    return s_current;
}

int fl_session_is_elevated_account(void) {
    int elevated;

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    elevated = fl_user_db_is_elevated_user(&s_db, s_current);
    pthread_mutex_unlock(&s_session_mu);
    return elevated;
}

int fl_session_in_sudo_scope(void) {
    int in_scope;

    pthread_mutex_lock(&s_session_mu);
    in_scope = s_sudo_scope_depth > 0;
    pthread_mutex_unlock(&s_session_mu);
    return in_scope;
}

void fl_session_begin_sudo_scope(void) {
    pthread_mutex_lock(&s_session_mu);
    s_sudo_scope_depth++;
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_end_sudo_scope(void) {
    pthread_mutex_lock(&s_session_mu);
    if (s_sudo_scope_depth > 0)
        s_sudo_scope_depth--;
    pthread_mutex_unlock(&s_session_mu);
}

int fl_session_jail_privileged(void) {
    int privileged;

    pthread_mutex_lock(&s_session_mu);
    privileged = s_sudo_scope_depth > 0;
    if (!privileged && s_db_ready)
        privileged = fl_user_db_is_elevated_user(&s_db, s_current);
    pthread_mutex_unlock(&s_session_mu);
    return privileged;
}

int fl_session_has_elevation(void) {
    int has;

    pthread_mutex_lock(&s_session_mu);
    if (s_sudo_scope_depth > 0)
        has = 1;
    else if (fl_elevation_active(s_active_elev))
        has = 1;
    else if (!s_db_ready)
        has = 0;
    else
        has = fl_user_db_is_elevated_user(&s_db, s_current);
    pthread_mutex_unlock(&s_session_mu);
    return has;
}

fl_result_t fl_session_login(const char *name, const char *password) {
    fl_result_t rc = FL_RESULT_ACCES;

    fl_session_init();
    if (!name || !name[0] || !password)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    if (fl_user_db_verify_password(&s_db, name, password)) {
        fl_elevation_revoke_all();
        strncpy(s_current, name, sizeof(s_current) - 1);
        s_current[sizeof(s_current) - 1] = '\0';
        s_active_elev = FL_ELEVATION_TOKEN_NONE;
        s_sudo_scope_depth = 0;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}

int fl_session_verify_password(const char *password) {
    int ok = 0;

    fl_session_init();
    if (!password)
        return 0;
    pthread_mutex_lock(&s_session_mu);
    ok = fl_user_db_verify_password(&s_db, s_current, password) ? 1 : 0;
    pthread_mutex_unlock(&s_session_mu);
    return ok;
}

fl_result_t fl_session_set_user(const char *name) {
    const fl_user_record_t *u;
    fl_result_t rc = FL_RESULT_NOENT;

    fl_session_init();
    if (!name || !name[0])
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_session_mu);
    u = fl_user_db_find(&s_db, name);
    if (u) {
        fl_elevation_revoke_all();
        strncpy(s_current, name, sizeof(s_current) - 1);
        s_current[sizeof(s_current) - 1] = '\0';
        s_active_elev = FL_ELEVATION_TOKEN_NONE;
        s_sudo_scope_depth = 0;
        rc = FL_RESULT_OK;
    }
    pthread_mutex_unlock(&s_session_mu);
    return rc;
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
    pthread_mutex_lock(&s_session_mu);
    rc = fl_elevation_grant(s_current, reason, out);
    if (rc == FL_RESULT_OK)
        s_active_elev = *out;
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}

void fl_session_bind_elevation(fl_elevation_token_t token) {
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    s_active_elev = token;
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_clear_elevation(void) {
    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    if (s_active_elev != FL_ELEVATION_TOKEN_NONE) {
        fl_elevation_revoke(s_active_elev);
        s_active_elev = FL_ELEVATION_TOKEN_NONE;
    }
    pthread_mutex_unlock(&s_session_mu);
}

void fl_session_drop_elevation(void) {
    fl_session_init();
    fl_session_clear_elevation();
    pthread_mutex_lock(&s_session_mu);
    s_sudo_scope_depth = 0;
    pthread_mutex_unlock(&s_session_mu);
}

fl_result_t fl_session_logout(void) {
    const char *def;

    fl_session_init();
    fl_elevation_revoke_all();
    pthread_mutex_lock(&s_session_mu);
    s_sudo_scope_depth = 0;
    s_active_elev = FL_ELEVATION_TOKEN_NONE;
    def = s_db.default_user;
    if (!def || !def[0])
        def = "flinstone";
    strncpy(s_current, def, sizeof(s_current) - 1);
    s_current[sizeof(s_current) - 1] = '\0';
    pthread_mutex_unlock(&s_session_mu);
    return FL_RESULT_OK;
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
    fl_result_t rc;

    fl_session_init();
    pthread_mutex_lock(&s_session_mu);
    rc = fl_user_db_save(&s_db);
    pthread_mutex_unlock(&s_session_mu);
    return rc;
}
