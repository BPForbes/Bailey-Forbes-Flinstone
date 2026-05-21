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

fl_result_t fl_user_db_load(fl_user_db_t *db, const char *path) {
    FILE *f;
    char line[512];
    fl_user_record_t cur;
    int in_user = 0;

    if (!db || !path)
        return FL_RESULT_INVAL;
    fl_user_db_clear(db);

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
        if (strstr(line, "\"uid\"")) {
            if (!in_user) {
                mem_domain_zero(&cur, sizeof(cur));
                in_user = 1;
            }
            parse_int_field(line, "\"uid\"", (int *)&cur.uid);
        }
        if (strstr(line, "\"is_root\"")) {
            if (!in_user) {
                mem_domain_zero(&cur, sizeof(cur));
                in_user = 1;
            }
            parse_int_field(line, "\"is_root\"", &cur.is_root);
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

int fl_user_db_is_root_user(const fl_user_db_t *db, const char *name) {
    const fl_user_record_t *u = fl_user_db_find(db, name);
    return (u && u->is_root) ? 1 : 0;
}
