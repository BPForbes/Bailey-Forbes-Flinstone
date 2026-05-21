#include "session.h"
#include "elevation.h"
#include <pthread.h>
#include <string.h>

static fl_user_db_t s_db;
static char s_current[FL_USER_NAME_MAX];
static fl_elevation_token_t s_active_elev = FL_ELEVATION_TOKEN_NONE;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void session_load_once(void) {
    if (fl_user_db_load(&s_db, FL_SESSION_USERS_PATH) != FL_RESULT_OK) {
        fl_user_db_clear(&s_db);
        strncpy(s_db.default_user, "user", sizeof(s_db.default_user) - 1);
        if (s_db.count < FL_USER_DB_MAX_USERS) {
            strncpy(s_db.users[0].name, "user", sizeof(s_db.users[0].name) - 1);
            s_db.users[0].uid = 1000;
            s_db.count = 1;
        }
        s_db.loaded = 1;
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

int fl_session_is_root(void) {
    fl_session_init();
    return fl_user_db_is_root_user(&s_db, s_current);
}

int fl_session_has_elevation(void) {
    if (fl_session_is_root())
        return 1;
    return fl_elevation_active(s_active_elev);
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
    return FL_RESULT_OK;
}

fl_result_t fl_session_grant_elevation(const char *reason, fl_elevation_token_t *out) {
    fl_result_t rc;
    fl_session_init();
    if (!out)
        return FL_RESULT_INVAL;
    rc = fl_elevation_grant(s_current, reason, out);
    if (rc == FL_RESULT_OK)
        s_active_elev = *out;
    return rc;
}

void fl_session_bind_elevation(fl_elevation_token_t token) {
    fl_session_init();
    s_active_elev = token;
}

const fl_user_db_t *fl_session_user_db(void) {
    fl_session_init();
    return &s_db;
}
