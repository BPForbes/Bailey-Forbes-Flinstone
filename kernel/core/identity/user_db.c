#include "user_db.h"
#include "mem_domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s) {
    char *e;
    if (!s || !s[0])
        return;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        memmove(s, s + 1, strlen(s));
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
        *--e = '\0';
}

static int parse_quoted_value(const char *line, const char *key, char *out, size_t outsz) {
    const char *p = strstr(line, key);
    const char *q;
    size_t n;
    if (!p)
        return 0;
    p = strchr(p + strlen(key), '"');
    if (!p)
        return 0;
    q = strchr(p + 1, '"');
    if (!q)
        return 0;
    n = (size_t)(q - (p + 1));
    if (n >= outsz)
        n = outsz - 1;
    memcpy(out, p + 1, n);
    out[n] = '\0';
    return 1;
}

static int parse_int_field(const char *line, const char *key, int *out) {
    const char *p = strstr(line, key);
    char *end = NULL;
    long v;
    if (!p)
        return 0;
    p = strchr(p + strlen(key), ':');
    if (!p)
        return 0;
    p++;
    v = strtol(p, &end, 10);
    if (end == p)
        return 0;
    *out = (int)v;
    return 1;
}

void fl_user_db_seed_defaults(fl_user_db_t *db) {
    if (!db)
        return;
    fl_user_db_clear(db);
    strncpy(db->default_user, "flinstone", sizeof(db->default_user) - 1);
    fl_user_db_add_user(db, "flinstone", "flinstone", 1000, 0);
    fl_user_db_add_user(db, "root", "root", 0, 1);
    db->loaded = 1;
}

fl_result_t fl_user_db_load(fl_user_db_t *db, const char *path) {
    FILE *f;
    char line[512];
    fl_user_record_t cur;
    int in_user = 0;

    if (!db || !path)
        return FL_RESULT_INVAL;
    fl_user_db_clear(db);
    strncpy(db->path, path, sizeof(db->path) - 1);

    f = fopen(path, "r");
    if (!f)
        return FL_RESULT_NOENT;

    mem_domain_zero(&cur, sizeof(cur));
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (strstr(line, "\"default_user\"")) {
            parse_quoted_value(line, "\"default_user\"", db->default_user, sizeof(db->default_user));
            continue;
        }
        if (strstr(line, "\"users\""))
            continue;
        if (strstr(line, "\"name\"")) {
            if (!in_user) {
                mem_domain_zero(&cur, sizeof(cur));
                in_user = 1;
            }
            parse_quoted_value(line, "\"name\"", cur.name, sizeof(cur.name));
        }
        if (strstr(line, "\"password\"")) {
            if (!in_user) {
                mem_domain_zero(&cur, sizeof(cur));
                in_user = 1;
            }
            parse_quoted_value(line, "\"password\"", cur.password, sizeof(cur.password));
        }
        if (strstr(line, "\"uid\"")) {
            if (!in_user) {
                mem_domain_zero(&cur, sizeof(cur));
                in_user = 1;
            }
            parse_int_field(line, "\"uid\"", (int *)&cur.uid);
        }
        if (strstr(line, "\"is_elevated\"") || strstr(line, "\"is_root\"")) {
            int v = 0;
            if (!in_user) {
                mem_domain_zero(&cur, sizeof(cur));
                in_user = 1;
            }
            if (strstr(line, "\"is_elevated\""))
                parse_int_field(line, "\"is_elevated\"", &v);
            else
                parse_int_field(line, "\"is_root\"", &v);
            cur.is_elevated = (v != 0) ? 1 : 0;
        }
        if (in_user && strchr(line, '}')) {
            if (cur.name[0] && db->count < FL_USER_DB_MAX_USERS)
                db->users[db->count++] = cur;
            mem_domain_zero(&cur, sizeof(cur));
            in_user = 0;
        }
    }
    if (in_user && cur.name[0] && db->count < FL_USER_DB_MAX_USERS)
        db->users[db->count++] = cur;
    fclose(f);

    if (!db->default_user[0] && db->count > 0)
        strncpy(db->default_user, db->users[0].name, sizeof(db->default_user) - 1);
    db->loaded = 1;
    return FL_RESULT_OK;
}

fl_result_t fl_user_db_save(const fl_user_db_t *db) {
    FILE *f;
    size_t i;
    const char *path;

    if (!db || !db->loaded)
        return FL_RESULT_INVAL;
    path = db->path[0] ? db->path : FL_USERS_DB_DEFAULT_PATH;
    f = fopen(path, "w");
    if (!f)
        return FL_RESULT_ERR;
    fprintf(f, "{\n  \"default_user\": \"%s\",\n  \"users\": [\n",
            db->default_user[0] ? db->default_user : "flinstone");
    for (i = 0; i < db->count; i++) {
        fprintf(f,
                "    {\n      \"name\": \"%s\",\n      \"password\": \"%s\",\n"
                "      \"uid\": %u,\n      \"is_elevated\": %d\n    }%s\n",
                db->users[i].name, db->users[i].password,
                (unsigned)db->users[i].uid, db->users[i].is_elevated,
                (i + 1 < db->count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return FL_RESULT_OK;
}

void fl_user_db_clear(fl_user_db_t *db) {
    if (!db)
        return;
    mem_domain_zero(db, sizeof(*db));
}

const fl_user_record_t *fl_user_db_find(const fl_user_db_t *db, const char *name) {
    size_t i;
    if (!db || !name || !name[0])
        return NULL;
    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].name, name) == 0)
            return &db->users[i];
    }
    return NULL;
}

int fl_user_db_verify_password(const fl_user_db_t *db, const char *name, const char *password) {
    const fl_user_record_t *u;
    if (!db || !name || !password)
        return 0;
    u = fl_user_db_find(db, name);
    if (!u)
        return 0;
    return strcmp(u->password, password) == 0;
}

int fl_user_db_is_elevated_user(const fl_user_db_t *db, const char *name) {
    const fl_user_record_t *u = fl_user_db_find(db, name);
    return (u && u->is_elevated) ? 1 : 0;
}

fl_result_t fl_user_db_add_user(fl_user_db_t *db, const char *name, const char *password,
                                uint32_t uid, int is_elevated) {
    fl_user_record_t *u;
    if (!db || !name || !name[0] || !password)
        return FL_RESULT_INVAL;
    if (fl_user_db_find(db, name))
        return FL_RESULT_BUSY;
    if (db->count >= FL_USER_DB_MAX_USERS)
        return FL_RESULT_NOMEM;
    u = &db->users[db->count++];
    mem_domain_zero(u, sizeof(*u));
    strncpy(u->name, name, sizeof(u->name) - 1);
    strncpy(u->password, password, sizeof(u->password) - 1);
    u->uid = uid;
    u->is_elevated = is_elevated ? 1 : 0;
    db->loaded = 1;
    return FL_RESULT_OK;
}

fl_result_t fl_user_db_remove_user(fl_user_db_t *db, const char *name) {
    size_t i;
    if (!db || !name)
        return FL_RESULT_INVAL;
    if (strcmp(name, "root") == 0)
        return FL_RESULT_ACCES;
    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].name, name) == 0) {
            if (db->count - 1 > i)
                db->users[i] = db->users[db->count - 1];
            db->count--;
            if (strcmp(db->default_user, name) == 0 && db->count > 0)
                strncpy(db->default_user, db->users[0].name, sizeof(db->default_user) - 1);
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_NOENT;
}

fl_result_t fl_user_db_set_password(fl_user_db_t *db, const char *name, const char *password) {
    fl_user_record_t *u;
    size_t i;
    if (!db || !name || !password)
        return FL_RESULT_INVAL;
    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].name, name) == 0) {
            u = &db->users[i];
            strncpy(u->password, password, sizeof(u->password) - 1);
            u->password[sizeof(u->password) - 1] = '\0';
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_NOENT;
}
