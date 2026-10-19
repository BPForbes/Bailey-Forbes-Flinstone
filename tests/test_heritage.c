/**
 * test_heritage.c - account heritage key ids, migration, bridges and account homes
 * (contracts/identity/contract_p2_heritage.h; kernel/core/identity/{heritage,bridge,user_db}.c,
 * kernel/core/vfs/fs_jail.c home checks).
 */
#include "heritage.h"
#include "bridge.h"
#include "user_db.h"
#include "session.h"
#include "fs_jail.h"
#include "common.h"
#include <dirent.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char g_fs_jail_root[];

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

static int temp_db(char *out, size_t cap) {
    char tmpl[] = "/tmp/fl_heritage_testXXXXXX";
    int fd = mkstemp(tmpl);

    if (fd < 0)
        return -1;
    close(fd);
    unlink(tmpl);
    snprintf(out, cap, "%s.db", tmpl);
    return 0;
}

static void rmrf(const char *path) {
    DIR *d = opendir(path);
    struct dirent *e;

    if (!d) {
        unlink(path);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char sub[PATH_MAX];
        struct stat st;

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode))
            rmrf(sub);
        else
            unlink(sub);
    }
    closedir(d);
    rmdir(path);
}

static int test_key_ids(void) {
    char out[FL_HERITAGE_KEY_ID_MAX];
    char deep[FL_HERITAGE_KEY_ID_MAX];
    unsigned i;

    ASSERT(fl_heritage_key_valid("Root"));
    ASSERT(fl_heritage_key_valid("Root.Flinstone.Bob"));
    ASSERT(!fl_heritage_key_valid("root"));
    ASSERT(!fl_heritage_key_valid("Rooted"));
    ASSERT(!fl_heritage_key_valid("Flinstone"));
    ASSERT(!fl_heritage_key_valid("Root."));
    ASSERT(!fl_heritage_key_valid("Root..Bob"));
    ASSERT(!fl_heritage_key_valid("Root.Bo b"));
    ASSERT(!fl_heritage_key_valid("Root.a/b"));
    ASSERT(!fl_heritage_key_valid("Root.root"));
    ASSERT(!fl_heritage_key_valid(NULL));
    ASSERT(fl_heritage_depth("Root") == 1u);
    ASSERT(fl_heritage_depth("Root.Flinstone.Bob") == 3u);
    ASSERT(strcmp(fl_heritage_leaf("Root.Flinstone.Bob"), "Bob") == 0);
    ASSERT(strcmp(fl_heritage_leaf("Root"), "Root") == 0);

    ASSERT(fl_heritage_child("Root.Flinstone", "Bob", out, sizeof out) == FL_RESULT_OK);
    ASSERT(strcmp(out, "Root.Flinstone.Bob") == 0);
    ASSERT(fl_heritage_child("Root", "Root", out, sizeof out) == FL_RESULT_INVAL);
    ASSERT(fl_heritage_child("Root", "ROOT", out, sizeof out) == FL_RESULT_INVAL);
    ASSERT(fl_heritage_child("Root", "a.b", out, sizeof out) == FL_RESULT_INVAL);
    ASSERT(fl_heritage_child("Root", "", out, sizeof out) == FL_RESULT_INVAL);
    ASSERT(fl_heritage_child("Root", "abcdefghijklmnopqrstuvwxyz123456", out, sizeof out)
           == FL_RESULT_INVAL); /* 32 chars: one too long */

    /* At most FL_HERITAGE_MAX_DEPTH segments. */
    strcpy(deep, "Root");
    for (i = 1; i < FL_HERITAGE_MAX_DEPTH; i++) {
        char next[FL_HERITAGE_KEY_ID_MAX];
        ASSERT(fl_heritage_child(deep, "L", next, sizeof next) == FL_RESULT_OK);
        strcpy(deep, next);
    }
    ASSERT(fl_heritage_depth(deep) == FL_HERITAGE_MAX_DEPTH);
    ASSERT(fl_heritage_child(deep, "TooDeep", out, sizeof out) == FL_RESULT_NOMEM);

    ASSERT(fl_heritage_parent("Root.Flinstone.Bob", out, sizeof out) == FL_RESULT_OK);
    ASSERT(strcmp(out, "Root.Flinstone") == 0);
    ASSERT(fl_heritage_parent("Root", out, sizeof out) == FL_RESULT_NOENT);

    ASSERT(fl_heritage_home_rel("Root", out, sizeof out) == FL_RESULT_OK);
    ASSERT(strcmp(out, "home") == 0);
    ASSERT(fl_heritage_home_rel("Root.Flinstone.Bob", out, sizeof out) == FL_RESULT_OK);
    ASSERT(strcmp(out, "home/Flinstone/Bob") == 0);
    return 0;
}

static int test_relations(void) {
    ASSERT(fl_heritage_relation("Root.Flinstone", "Root.Flinstone") == FL_HERITAGE_REL_SELF);
    ASSERT(fl_heritage_relation("Root", "Root.Flinstone.Bob") == FL_HERITAGE_REL_DESCENDANT);
    ASSERT(fl_heritage_relation("Root.Flinstone", "Root.Flinstone.Bob") == FL_HERITAGE_REL_DESCENDANT);
    ASSERT(fl_heritage_relation("Root.Flinstone.Bob", "Root.Flinstone") == FL_HERITAGE_REL_ANCESTOR);
    ASSERT(fl_heritage_relation("Root.Flinstone", "Root.Alternative") == FL_HERITAGE_REL_SIBLING);
    ASSERT(fl_heritage_relation("Root.Flinstone.Bob", "Root.Alternative.Ann")
           == FL_HERITAGE_REL_OTHER_BRANCH);
    ASSERT(fl_heritage_relation("Root.Flinstone.Bob", "Root.Alternative")
           == FL_HERITAGE_REL_OTHER_BRANCH);
    /* A shared prefix is not ancestry: Root.Flin is not above Root.Flinstone. */
    ASSERT(!fl_heritage_is_ancestor_or_self("Root.Flin", "Root.Flinstone"));
    ASSERT(fl_heritage_relation("Root.Flin", "Root.Flinstone") == FL_HERITAGE_REL_SIBLING);
    ASSERT(fl_heritage_relation("Root", "nope") == FL_HERITAGE_REL_INVALID);
    return 0;
}

static int test_seed_and_accounts(void) {
    char path[256];
    fl_user_db_t db;
    const fl_user_record_t *u;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(db.count == 3u);
    u = fl_user_db_find(&db, "Root");
    ASSERT(u && strcmp(u->key_id, "Root") == 0 && u->is_elevated && u->uid == 0u && !u->created_by[0]);
    u = fl_user_db_find(&db, "Flinstone");
    ASSERT(u && strcmp(u->key_id, "Root.Flinstone") == 0 && strcmp(u->created_by, "Root") == 0);
    u = fl_user_db_find(&db, "Alternative");
    ASSERT(u && strcmp(u->key_id, "Root.Alternative") == 0 && !u->is_elevated);
    ASSERT(strcmp(db.default_user, "Flinstone") == 0);
    ASSERT(fl_user_db_verify_password(&db, "root", "root"));
    ASSERT(fl_user_db_verify_password(&db, "FLINSTONE", "flinstone"));
    ASSERT(fl_user_db_verify_password(&db, "Alternative", "alternative"));

    /* Flinstone makes Bob; Bob makes Cara. */
    ASSERT(fl_user_db_add_user_under(&db, "Root.Flinstone", "Bob", "pw", 3000, 0, 1700000000)
           == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Flinstone.Bob", "Cara", "pw", 3001, 0, 0)
           == FL_RESULT_OK);
    u = fl_user_db_find(&db, "bob");
    ASSERT(u && strcmp(u->key_id, "Root.Flinstone.Bob") == 0);
    ASSERT(strcmp(u->created_by, "Root.Flinstone") == 0 && u->created_at == 1700000000);
    ASSERT(fl_user_db_find_by_key(&db, "Root.Flinstone.Bob.Cara"));
    ASSERT(fl_user_db_add_user_under(&db, "Root.Alternative", "BOB", "pw", 3002, 0, 0)
           == FL_RESULT_BUSY); /* names are unique in any case */
    ASSERT(fl_user_db_add_user_under(&db, "Root.Nobody", "Dee", "pw", 3003, 0, 0)
           == FL_RESULT_NOENT);
    ASSERT(fl_user_db_add_user_under(&db, "Root", "bad name", "pw", 3004, 0, 0) == FL_RESULT_INVAL);
    ASSERT(fl_user_db_add_user_under(&db, "Root", "root", "pw", 3005, 0, 0) == FL_RESULT_INVAL);

    ASSERT(fl_user_db_has_descendants(&db, "Root.Flinstone"));
    ASSERT(!fl_user_db_has_descendants(&db, "Root.Alternative"));
    ASSERT(fl_user_db_remove_user(&db, "Bob") == FL_RESULT_BUSY);
    ASSERT(fl_user_db_remove_user(&db, "ROOT") == FL_RESULT_ACCES);

    /* Key ids, creators and times survive a save and reload. */
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    u = fl_user_db_find(&db, "Cara");
    ASSERT(u && strcmp(u->key_id, "Root.Flinstone.Bob.Cara") == 0 &&
           strcmp(u->created_by, "Root.Flinstone.Bob") == 0);
    u = fl_user_db_find(&db, "Bob");
    ASSERT(u && u->created_at == 1700000000);
    ASSERT(fl_user_db_remove_user(&db, "Cara") == FL_RESULT_OK);
    ASSERT(fl_user_db_remove_user(&db, "Bob") == FL_RESULT_OK);
    unlink(path);
    return 0;
}

static int test_legacy_migration(void) {
    char path[256];
    sqlite3 *sql = NULL;
    fl_user_db_t seeded;
    fl_user_db_t db;
    const fl_user_record_t *u;
    size_t i;

    ASSERT(temp_db(path, sizeof path) == 0);
    /* A store written before heritage: no key_id, created_by or created_at columns. */
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql,
                        "CREATE TABLE users (name TEXT PRIMARY KEY NOT NULL, password_hash TEXT NOT NULL,"
                        " salt_hex TEXT NOT NULL, uid INTEGER NOT NULL, is_elevated INTEGER NOT NULL DEFAULT 0);"
                        "CREATE TABLE meta (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);"
                        "INSERT INTO meta VALUES('default_user','flinstone');",
                        NULL, NULL, NULL) == SQLITE_OK);
    /* Real hashes for the legacy rows, borrowed from a lab seed. */
    fl_user_db_clear(&seeded);
    fl_user_db_seed_defaults(&seeded);
    for (i = 0; i < seeded.count; i++) {
        const fl_user_record_t *s = &seeded.users[i];
        const char *legacy = !strcmp(s->name, "Root") ? "root"
                             : !strcmp(s->name, "Flinstone") ? "flinstone" : "alice";
        char stmt[512];

        snprintf(stmt, sizeof stmt,
                 "INSERT INTO users VALUES('%s','%s','%s',%u,%d);", legacy, s->password_hash,
                 s->salt_hex, !strcmp(legacy, "alice") ? 2000u : s->uid, s->is_elevated);
        ASSERT(sqlite3_exec(sql, stmt, NULL, NULL, NULL) == SQLITE_OK);
    }
    ASSERT(sqlite3_exec(sql, "INSERT INTO users VALUES('odd name','x','00',2001,0);", NULL, NULL,
                        NULL) == SQLITE_OK);
    sqlite3_close(sql);

    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    u = fl_user_db_find(&db, "root");
    ASSERT(u && strcmp(u->name, "Root") == 0 && strcmp(u->key_id, "Root") == 0);
    u = fl_user_db_find(&db, "flinstone");
    ASSERT(u && strcmp(u->name, "Flinstone") == 0 && strcmp(u->key_id, "Root.Flinstone") == 0);
    ASSERT(fl_user_db_verify_password(&db, "Flinstone", "flinstone"));
    u = fl_user_db_find(&db, "alice");
    ASSERT(u && strcmp(u->key_id, "Root.alice") == 0 && strcmp(u->created_by, "Root") == 0 &&
           u->created_at == 0);
    u = fl_user_db_find(&db, "odd name");
    ASSERT(u && !u->key_id[0]); /* not a valid segment: no key id, no authority */
    ASSERT(fl_user_db_find(&db, "Alternative")); /* lab defaults on: second default account added */
    ASSERT(strcmp(db.default_user, "Flinstone") == 0);

    /* The migration was written back: a second load changes nothing and agrees. */
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    u = fl_user_db_find(&db, "Root");
    ASSERT(u && strcmp(u->name, "Root") == 0 && strcmp(u->key_id, "Root") == 0);
    ASSERT(strcmp(fl_user_db_find(&db, "alice")->key_id, "Root.alice") == 0);
    unlink(path);
    return 0;
}

/* Two legacy names that differ only by case must not be rewritten onto one key id. */
static int test_legacy_case_collision(void) {
    char path[256];
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_user_db_t db;
    int rows = 0;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql,
                        "CREATE TABLE users (name TEXT PRIMARY KEY NOT NULL, password_hash TEXT NOT NULL,"
                        " salt_hex TEXT NOT NULL, uid INTEGER NOT NULL, is_elevated INTEGER NOT NULL DEFAULT 0);"
                        "CREATE TABLE meta (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);"
                        "INSERT INTO users VALUES('root','x','00',1000,1);"
                        "INSERT INTO users VALUES('Root','y','11',2000,0);",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);

    ASSERT(fl_user_db_load(&db, path) != FL_RESULT_OK);

    /* Nothing was rewritten: both legacy rows are still there, under their own names. */
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_prepare_v2(sql, "SELECT name, key_id FROM users ORDER BY uid;", -1, &st, NULL)
           == SQLITE_OK);
    while (sqlite3_step(st) == SQLITE_ROW) {
        ASSERT(rows == 0 ? strcmp((const char *)sqlite3_column_text(st, 0), "root") == 0
                         : strcmp((const char *)sqlite3_column_text(st, 0), "Root") == 0);
        ASSERT(((const char *)sqlite3_column_text(st, 1))[0] == '\0');
        rows++;
    }
    ASSERT(rows == 2);
    sqlite3_finalize(st);
    sqlite3_close(sql);
    unlink(path);
    return 0;
}

static unsigned g_visited;
static void count_bridge(const fl_bridge_t *b, void *ctx) {
    (void)b;
    (void)ctx;
    g_visited++;
}

static int test_bridges(void) {
    char path[256];
    fl_user_db_t db;
    fl_bridge_t b;
    int64_t id = 0;
    int64_t id2 = 0;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Flinstone", "Bob", "pw", 3000, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Alternative", "Ann", "pw", 3001, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);

    /* One line needs no bridge. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone", "Root.Flinstone.Bob", 1, &id)
           == FL_RESULT_INVAL);
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root", 1, &id) == FL_RESULT_INVAL);
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Ghost", 1, &id)
           == FL_RESULT_INVAL);

    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 10, &id)
           == FL_RESULT_OK && id > 0);
    ASSERT(fl_bridge_request(path, &db, "Root.Alternative.Ann", "Root.Flinstone.Bob", 11, &id2)
           == FL_RESULT_BUSY);
    /* Pending grants nothing. */
    ASSERT(!fl_bridge_active_between(path, "Root.Flinstone.Bob", "Root.Alternative.Ann"));
    /* The requesting side, or its ancestor, cannot accept. */
    ASSERT(fl_bridge_accept(path, "Root.Flinstone.Bob", id, 12) == FL_RESULT_ACCES);
    ASSERT(fl_bridge_accept(path, "Root.Flinstone", id, 12) == FL_RESULT_ACCES);
    /* Ann's ancestor may accept for her. */
    ASSERT(fl_bridge_accept(path, "Root.Alternative", id, 13) == FL_RESULT_OK);
    ASSERT(fl_bridge_accept(path, "Root.Alternative.Ann", id, 14) == FL_RESULT_BUSY);
    ASSERT(fl_bridge_get(path, id, &b) == FL_RESULT_OK);
    ASSERT(b.state == FL_BRIDGE_STATE_ACTIVE && strcmp(b.accepted_by, "Root.Alternative") == 0 &&
           b.accepted_at == 13);
    ASSERT(fl_bridge_active_between(path, "Root.Alternative.Ann", "Root.Flinstone.Bob"));
    /* A bridge joins exactly its two accounts: no inheritance either way. */
    ASSERT(!fl_bridge_active_between(path, "Root.Flinstone", "Root.Alternative.Ann"));
    ASSERT(!fl_bridge_active_between(path, "Root.Flinstone.Bob", "Root.Alternative"));

    g_visited = 0;
    ASSERT(fl_bridge_list_visible(path, "Root.Flinstone", count_bridge, NULL) == FL_RESULT_OK);
    ASSERT(g_visited == 1u);
    g_visited = 0;
    ASSERT(fl_bridge_list_visible(path, "Root.Alternative.Ann", count_bridge, NULL) == FL_RESULT_OK);
    ASSERT(g_visited == 1u);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Alternative", "Cy", "pw", 3002, 0, 0) == FL_RESULT_OK);
    g_visited = 0;
    ASSERT(fl_bridge_list_visible(path, "Root.Alternative.Cy", count_bridge, NULL) == FL_RESULT_OK);
    ASSERT(g_visited == 0u);

    /* Revocation: not a third party, yes an ancestor of either side. */
    ASSERT(fl_bridge_revoke(path, "Root.Alternative.Cy", id) == FL_RESULT_ACCES);
    ASSERT(fl_bridge_revoke(path, "Root.Flinstone", id) == FL_RESULT_OK);
    ASSERT(!fl_bridge_active_between(path, "Root.Flinstone.Bob", "Root.Alternative.Ann"));
    ASSERT(fl_bridge_get(path, id, &b) == FL_RESULT_NOENT);

    /* Removing an account removes its bridges. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 20, &id2)
           == FL_RESULT_OK);
    ASSERT(fl_bridge_remove_for(path, "Root.Alternative.Ann") == FL_RESULT_OK);
    ASSERT(fl_bridge_get(path, id2, &b) == FL_RESULT_NOENT);

    /* user_db saves never touch the bridge table. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone", "Root.Alternative", 30, &id) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);
    ASSERT(fl_bridge_get(path, id, &b) == FL_RESULT_OK);
    unlink(path);
    return 0;
}

/* Account homes in an active VM jail, through the session singleton. */
static int test_jail_homes(const char *jail_parent) {
    char home[PATH_MAX];
    char p[PATH_MAX];
    fl_user_db_t *db;
    int64_t id = 0;

    g_vm_mode = 1;
    strncpy(g_vm_root, jail_parent, sizeof(g_vm_root) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active());

    fl_session_init();
    db = fl_session_user_db_mut();
    ASSERT(fl_user_db_add_user_under(db, "Root.Flinstone", "Bob", "pw", 3000, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_session_save_users() == FL_RESULT_OK);

    ASSERT(fs_jail_account_home("Root.Flinstone.Bob", home, sizeof home) == 0);
    snprintf(p, sizeof p, "%s/home/Flinstone/Bob", g_fs_jail_root);
    ASSERT(strcmp(home, p) == 0);
    {
        struct stat st;
        ASSERT(stat(home, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 077) == 0);
    }
    ASSERT(fs_jail_account_home("Root.Alternative", home, sizeof home) == 0);
    ASSERT(fs_jail_account_home("not a key", home, sizeof home) == -1);

    /* Flinstone: own home and Bob's (descendant), not Alternative's (sibling). */
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    snprintf(p, sizeof p, "%s/home/Flinstone/notes.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == 0);
    snprintf(p, sizeof p, "%s/home/Flinstone/Bob/new/file.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == 0);
    snprintf(p, sizeof p, "%s/home/Alternative/secret.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == -1);
    ASSERT(fs_jail_check_home(p) == -1);
    /* The home tree itself is Root's; outside it nothing changes. */
    snprintf(p, sizeof p, "%s/home", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == -1);
    snprintf(p, sizeof p, "%s/shared.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == 0);
    /* "..": canonical paths decide, not the spelling. */
    snprintf(p, sizeof p, "%s/home/Flinstone/../Alternative/x", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == -1);

    /* Bob: own home only; his creator's files are above him. */
    ASSERT(fl_session_set_user("Bob") == FL_RESULT_OK);
    snprintf(p, sizeof p, "%s/home/Flinstone/Bob/mine.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == 0);
    snprintf(p, sizeof p, "%s/home/Flinstone/notes.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == -1);
    /* sudo lifts the outer jail, never heritage. */
    fl_session_begin_sudo_scope();
    snprintf(p, sizeof p, "%s/home/Alternative/secret.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == -1);
    fl_session_end_sudo_scope();

    /* An active bridge Bob <-> Alternative opens Alternative's home to Bob only. */
    ASSERT(fl_bridge_request(db->path, db, "Root.Flinstone.Bob", "Root.Alternative", 1, &id)
           == FL_RESULT_OK);
    ASSERT(fs_jail_check_access(p) == -1); /* pending */
    ASSERT(fl_bridge_accept(db->path, "Root.Alternative", id, 2) == FL_RESULT_OK);
    ASSERT(fs_jail_check_access(p) == 0);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    ASSERT(fs_jail_check_access(p) == -1); /* Bob's ancestor does not inherit it */
    ASSERT(fl_session_set_user("Alternative") == FL_RESULT_OK);
    snprintf(p, sizeof p, "%s/home/Flinstone/Bob/mine.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == 0); /* both ways */
    snprintf(p, sizeof p, "%s/home/Flinstone/notes.txt", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == -1);

    /* Root reaches every home. */
    ASSERT(fl_session_set_user("root") == FL_RESULT_OK);
    ASSERT(strcmp(fl_session_current_key_id(), "Root") == 0);
    ASSERT(fs_jail_check_access(p) == 0);
    snprintf(p, sizeof p, "%s/home", g_fs_jail_root);
    ASSERT(fs_jail_check_access(p) == 0);

    g_vm_mode = 0;
    fs_jail_init();
    ASSERT(!fs_jail_is_active());
    ASSERT(fs_jail_check_home("/anything/home/x") == 0);
    return 0;
}

int main(void) {
    char db_path[256];
    char tmpl[] = "/tmp/fl_heritage_jailXXXXXX";
    char *jail_parent;
    int rc = 0;

    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
    if (temp_db(db_path, sizeof db_path) != 0)
        return 1;
    (void)setenv("FL_USERS_DB_PATH", db_path, 1);
    if (!mkdtemp(tmpl))
        return 1;
    jail_parent = realpath(tmpl, NULL);

#define RUN(t, ...) do { \
        if (t(__VA_ARGS__) != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_key_ids);
    RUN(test_relations);
    RUN(test_seed_and_accounts);
    RUN(test_legacy_migration);
    RUN(test_legacy_case_collision);
    RUN(test_bridges);
    RUN(test_jail_homes, jail_parent);

    if (chdir("/tmp") != 0)
        rc = 1;
    rmrf(jail_parent);
    free(jail_parent);
    unlink(db_path);
    if (rc == 0)
        printf("All heritage tests passed.\n");
    return rc;
}
