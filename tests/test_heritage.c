/**
 * test_heritage.c - account heritage key ids, migration, bridges and account homes
 * (contracts/identity/contract_p2_heritage.h; kernel/core/identity/{heritage,bridge,user_db}.c,
 * kernel/core/vfs/fs_jail.c home checks).
 */
#include "heritage.h"
#include "bridge.h"
#include "fl/driver/keyquorum.h"
#include "fl/kq.h"
#include "mem_domain.h"
#include "share_gate.h"
#include "user_db.h"
#include "session.h"
#include "elevation.h"
#include "fl/authz_subsystem.h"
#include "cmd_authutil.h"
#include "fs_facade.h"
#include "fs_jail.h"

/* session_sync.c calls these. The deny path never starts a file service. */
file_manager_service_t *g_fm_service;
void fm_service_set_user(file_manager_service_t *svc, const char *user) {
    (void)svc;
    (void)user;
}
#include "common.h"
#include "cmd_decl.h"
#include <dirent.h>
#include <limits.h>
#include <sqlite3.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

/*
 * Test-only seam: the binary is linked with -Wl,--wrap=fl_kq_driver_get_or_register (Makefile,
 * test_heritage), so every caller in the kernel objects reaches the KeyQuorum driver through here.
 * With s_patch NULL it is the real driver; otherwise a copy of it that s_patch changed (an op set to
 * NULL or replaced), to show that callers fail closed. Production code has no such hook.
 */
const fl_kq_driver_t *__real_fl_kq_driver_get_or_register(void);
const fl_kq_driver_t *__wrap_fl_kq_driver_get_or_register(void);
static void (*s_patch)(fl_kq_driver_t *drv);
static fl_kq_driver_t s_patched;

const fl_kq_driver_t *__wrap_fl_kq_driver_get_or_register(void) {
    const fl_kq_driver_t *real = __real_fl_kq_driver_get_or_register();

    if (!s_patch || !real)
        return real;
    s_patched = *real;
    s_patch(&s_patched);
    return &s_patched;
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
    /* 31 characters is the longest name that fits FL_USER_NAME_MAX (32 with the NUL). */
    ASSERT(fl_heritage_child("Root", "abcdefghijklmnopqrstuvwxyz12345", out, sizeof out)
           == FL_RESULT_OK);
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
    /* Migrated accounts have no signing key until they sign in. */
    ASSERT(!fl_user_db_signing_public(&db, "Root.Flinstone"));
    {
        int created = 0;

        ASSERT(fl_user_db_ensure_key(&db, "Flinstone", "wrong", &created) == FL_RESULT_ACCES);
        ASSERT(fl_user_db_ensure_key(&db, "Flinstone", "flinstone", &created) == FL_RESULT_OK &&
               created == 1);
        ASSERT(fl_user_db_signing_public(&db, "Root.Flinstone"));
    }

    /* The migration was written back: a second load changes nothing and agrees. */
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    u = fl_user_db_find(&db, "Root");
    ASSERT(u && strcmp(u->name, "Root") == 0 && strcmp(u->key_id, "Root") == 0);
    ASSERT(strcmp(fl_user_db_find(&db, "alice")->key_id, "Root.alice") == 0);
    unlink(path);
    return 0;
}

/* A bridge's reach (KQ-19, kq_bridge_connects): one end holds the actor or an ancestor of it, the other
 * sits on the scope's line. One hop only, and a bridge that touches neither is no reach. */
static int test_bridge_reach(void) {
    const char *a = "Root.Flinstone.Bob", *b = "Root.Alternative.Ann";

    ASSERT(fl_heritage_bridge_connects(a, b, "Root.Flinstone.Bob", "Root.Alternative.Ann") == 1);
    ASSERT(fl_heritage_bridge_connects(b, a, "Root.Flinstone.Bob", "Root.Alternative.Ann") == 1); /* ends are unordered */
    /* the actor may sit below an end, the scope may be above or below the other end */
    ASSERT(fl_heritage_bridge_connects(a, b, "Root.Flinstone.Bob.Kid", "Root.Alternative.Ann") == 1);
    ASSERT(fl_heritage_bridge_connects(a, b, "Root.Flinstone.Bob", "Root.Alternative") == 1);
    ASSERT(fl_heritage_bridge_connects(a, b, "Root.Flinstone.Bob", "Root.Alternative.Ann.Doc") == 1);
    /* no reach: an actor under neither end, a scope off the other end's line, a shared prefix that is not ancestry */
    ASSERT(fl_heritage_bridge_connects(a, b, "Root.Third.Eve", "Root.Alternative.Ann") == 0);
    ASSERT(fl_heritage_bridge_connects(a, b, "Root.Flinstone.Bob", "Root.Third") == 0);
    ASSERT(fl_heritage_bridge_connects(a, b, "Root.Flinstone.Bo", "Root.Alternative.Ann") == 0);
    /* an invalid key and a null argument connect nothing */
    ASSERT(fl_heritage_bridge_connects(a, b, "nope", "Root.Alternative.Ann") == 0);
    ASSERT(fl_heritage_bridge_connects(a, NULL, "Root.Flinstone.Bob", "Root.Alternative.Ann") == 0);
    return 0;
}

/* FL-15: the login mode and the KQST pairing. A store written before auth_mode upgrades in place and
 * every account reads PASSWORD; the mode survives save and load (save re-inserts every record from
 * memory, so a field outside the INSERT would be lost); the KQST path is read from account_keys. */
static int test_auth_mode_and_kqst_path(void) {
    char path[256], kqst[128];
    sqlite3 *sql = NULL;
    fl_user_db_t db;
    const fl_user_record_t *u;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql,
                        "CREATE TABLE users (name TEXT PRIMARY KEY NOT NULL, password_hash TEXT NOT NULL,"
                        " salt_hex TEXT NOT NULL, uid INTEGER NOT NULL, is_elevated INTEGER NOT NULL DEFAULT 0,"
                        " key_id TEXT NOT NULL DEFAULT '', created_by TEXT NOT NULL DEFAULT '',"
                        " created_at INTEGER NOT NULL DEFAULT 0, signing_public TEXT NOT NULL DEFAULT '',"
                        " signing_sealed TEXT NOT NULL DEFAULT '', box_public TEXT NOT NULL DEFAULT '',"
                        " box_sealed TEXT NOT NULL DEFAULT '');"
                        "INSERT INTO users (name,password_hash,salt_hex,uid,key_id) VALUES ('Root','x','y',0,'Root');"
                        "INSERT INTO users (name,password_hash,salt_hex,uid,key_id,created_by)"
                        " VALUES ('Bob','x','y',2000,'Root.Bob','Root');",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);

    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    u = fl_user_db_find(&db, "Bob");
    ASSERT(u && u->auth_mode == FL_USER_AUTH_PASSWORD); /* nobody is locked out by the upgrade */
    ASSERT(fl_user_db_set_auth_mode(&db, "Bob", 7) == FL_RESULT_INVAL);
    ASSERT(fl_user_db_set_auth_mode(&db, "Nobody", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_NOENT);
    ASSERT(fl_user_db_set_auth_mode(&db, "Bob", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_find(&db, "Bob")->auth_mode == FL_USER_AUTH_PASSWORD_KQST);
    ASSERT(fl_user_db_find(&db, "Root")->auth_mode == FL_USER_AUTH_PASSWORD);

    /* no row, then a row without a path, then a path */
    ASSERT(fl_user_db_kqst_path(&db, "Root.Bob", kqst, sizeof kqst) == FL_RESULT_NOENT);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql,
                        "INSERT INTO account_keys (label,purpose,holds_share,minted_by,minted_at,generation,"
                        "kqst_path,device_id,signing_public,encryption_public,ledger_seq,event_hash)"
                        " VALUES ('Root.Bob','own',1,'Root',1,1,NULL,x'00',x'00',x'00',0,x'00');"
                        "INSERT INTO account_keys (label,purpose,holds_share,minted_by,minted_at,generation,"
                        "kqst_path,device_id,signing_public,encryption_public,ledger_seq,event_hash)"
                        " VALUES ('Root.Eve','own',1,'Root',1,1,'/keys/eve.kqst',x'00',x'00',x'00',0,x'00');",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_kqst_path(&db, "Root.Bob", kqst, sizeof kqst) == FL_RESULT_NOENT);
    ASSERT(fl_user_db_kqst_path(&db, "Root.Eve", kqst, sizeof kqst) == FL_RESULT_OK);
    ASSERT(strcmp(kqst, "/keys/eve.kqst") == 0);
    ASSERT(fl_user_db_kqst_path(&db, "Root.Eve", kqst, 4) == FL_RESULT_NOMEM);
    /* x'00' blobs are one byte: pairing requires full device id and public keys. */
    {
        unsigned char device_id[16], signing_public[32], encryption_public[32];

        ASSERT(fl_user_db_kqst_pairing(&db, "Root.Eve", device_id, signing_public, encryption_public) ==
               FL_RESULT_NOENT);
    }
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql,
                        "UPDATE account_keys SET device_id=x'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',"
                        " signing_public=x'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',"
                        " encryption_public=x'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'"
                        " WHERE label='Root.Eve';",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    {
        unsigned char device_id[16], signing_public[32], encryption_public[32];

        ASSERT(fl_user_db_kqst_pairing(&db, "Root.Eve", device_id, signing_public, encryption_public) ==
               FL_RESULT_OK);
        ASSERT(device_id[0] == 0xaa && signing_public[0] == 0xbb && encryption_public[0] == 0xcc);
    }
    unlink(path);
    return 0;
}

/* Regression guard for the shared column-ensure: it also passes on the pre-D9 code. A bridges table written before bridges were signed gains nonce, request_sig and accept_sig in place. */
static int test_legacy_bridge_migration(void) {
    char path[256];
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_bridge_t b;
    int found = 0;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql,
                        "CREATE TABLE bridges (id INTEGER PRIMARY KEY AUTOINCREMENT, a_key TEXT NOT NULL,"
                        " b_key TEXT NOT NULL, state INTEGER NOT NULL, accepted_by TEXT NOT NULL DEFAULT '',"
                        " created_at INTEGER NOT NULL DEFAULT 0, accepted_at INTEGER NOT NULL DEFAULT 0);"
                        "INSERT INTO bridges (a_key, b_key, state, created_at) VALUES"
                        " ('Root.Flinstone.Bob','Root.Alternative.Ann',0,7);",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);

    /* Opening through the public API migrates; the legacy row survives with empty signature fields. */
    ASSERT(fl_bridge_get(path, 1, &b) == FL_RESULT_OK);
    ASSERT(strcmp(b.a_key, "Root.Flinstone.Bob") == 0 && strcmp(b.b_key, "Root.Alternative.Ann") == 0);
    ASSERT(b.created_at == 7 && !b.nonce_hex[0] && !b.request_sig[0] && !b.accept_sig[0]);

    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_prepare_v2(sql, "SELECT name FROM pragma_table_info('bridges');", -1, &st, NULL) ==
           SQLITE_OK);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *c = (const char *)sqlite3_column_text(st, 0);

        if (c && (!strcmp(c, "nonce") || !strcmp(c, "request_sig") || !strcmp(c, "accept_sig")))
            found++;
    }
    sqlite3_finalize(st);
    sqlite3_close(sql);
    ASSERT(found == 3);
    /* A second open is a no-op. */
    ASSERT(fl_bridge_get(path, 1, &b) == FL_RESULT_OK);
    unlink(path);
    return 0;
}

/* Behavior change of D9: an existing column matches case-insensitively (was strcmp, then a duplicate ALTER failed). */
static int test_legacy_bridge_mixed_case_columns(void) {
    char path[256];
    sqlite3 *sql = NULL;
    fl_bridge_t b;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql,
                        "CREATE TABLE bridges (id INTEGER PRIMARY KEY AUTOINCREMENT, a_key TEXT NOT NULL,"
                        " b_key TEXT NOT NULL, state INTEGER NOT NULL, accepted_by TEXT NOT NULL DEFAULT '',"
                        " created_at INTEGER NOT NULL DEFAULT 0, accepted_at INTEGER NOT NULL DEFAULT 0,"
                        " Nonce TEXT NOT NULL DEFAULT '', REQUEST_SIG TEXT NOT NULL DEFAULT '');"
                        "INSERT INTO bridges (a_key, b_key, state, created_at) VALUES"
                        " ('Root.Flinstone.Bob','Root.Alternative.Ann',0,7);",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_bridge_get(path, 1, &b) == FL_RESULT_OK);
    ASSERT(b.created_at == 7);
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
    unsigned char bob[FL_USER_SIGNING_SECRET_LEN];
    unsigned char ann[FL_USER_SIGNING_SECRET_LEN];
    unsigned char flin[FL_USER_SIGNING_SECRET_LEN];
    unsigned char alt[FL_USER_SIGNING_SECRET_LEN];

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Flinstone", "Bob", "pw", 3000, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Alternative", "Ann", "pw", 3001, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(&db, "Bob", "pw", bob) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(&db, "Ann", "pw", ann) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(&db, "Flinstone", "flinstone", flin) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(&db, "Alternative", "alternative", alt) == FL_RESULT_OK);

    /* One line needs no bridge. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone", "Root.Flinstone.Bob", 1, flin, &id)
           == FL_RESULT_INVAL);
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root", 1, bob, &id) == FL_RESULT_INVAL);
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Ghost", 1, bob, &id)
           == FL_RESULT_INVAL);

    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 10, bob, &id)
           == FL_RESULT_OK && id > 0);
    ASSERT(fl_bridge_request(path, &db, "Root.Alternative.Ann", "Root.Flinstone.Bob", 11, ann, &id2)
           == FL_RESULT_BUSY);
    /* Pending grants nothing. */
    ASSERT(!fl_bridge_active_between(&db, "Root.Flinstone.Bob", "Root.Alternative.Ann"));
    /* The requesting side, or its ancestor, cannot accept. */
    ASSERT(fl_bridge_accept(path, &db, "Root.Flinstone.Bob", id, 12, bob) == FL_RESULT_ACCES);
    ASSERT(fl_bridge_accept(path, &db, "Root.Flinstone", id, 12, flin) == FL_RESULT_ACCES);
    /* Ann's ancestor may accept for her. */
    ASSERT(fl_bridge_accept(path, &db, "Root.Alternative", id, 13, alt) == FL_RESULT_OK);
    ASSERT(fl_bridge_accept(path, &db, "Root.Alternative.Ann", id, 14, ann) == FL_RESULT_BUSY);
    ASSERT(fl_bridge_get(path, id, &b) == FL_RESULT_OK);
    ASSERT(b.state == FL_BRIDGE_STATE_ACTIVE && strcmp(b.accepted_by, "Root.Alternative") == 0 &&
           b.accepted_at == 13);
    ASSERT(fl_bridge_active_between(&db, "Root.Alternative.Ann", "Root.Flinstone.Bob"));
    /* A bridge joins exactly its two accounts: no inheritance either way. */
    ASSERT(!fl_bridge_active_between(&db, "Root.Flinstone", "Root.Alternative.Ann"));
    ASSERT(!fl_bridge_active_between(&db, "Root.Flinstone.Bob", "Root.Alternative"));

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
    ASSERT(!fl_bridge_active_between(&db, "Root.Flinstone.Bob", "Root.Alternative.Ann"));
    ASSERT(fl_bridge_get(path, id, &b) == FL_RESULT_NOENT);

    /* Removing an account removes its bridges. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 20, bob, &id2)
           == FL_RESULT_OK);
    ASSERT(fl_bridge_remove_for(path, "Root.Alternative.Ann") == FL_RESULT_OK);
    ASSERT(fl_bridge_get(path, id2, &b) == FL_RESULT_NOENT);

    /* user_db saves never touch the bridge table. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone", "Root.Alternative", 30, flin, &id) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);
    ASSERT(fl_bridge_get(path, id, &b) == FL_RESULT_OK);
    mem_domain_zero(bob, sizeof bob);
    mem_domain_zero(ann, sizeof ann);
    mem_domain_zero(flin, sizeof flin);
    mem_domain_zero(alt, sizeof alt);
    unlink(path);
    return 0;
}

/* Account keys: sealed under the password, kept or replaced, and what makes a bridge count. */
static int test_signed_bridges(void) {
    char path[256];
    char stmt[256];
    fl_user_db_t db;
    fl_user_db_t again;
    sqlite3 *sql = NULL;
    int64_t id = 0;
    int created = 0;
    unsigned char bob[FL_USER_SIGNING_SECRET_LEN];
    unsigned char ann[FL_USER_SIGNING_SECRET_LEN];
    unsigned char before[FL_USER_SIGNING_PUBLIC_LEN];
    const unsigned char *pub;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Flinstone", "Bob", "pw", 3000, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Alternative", "Ann", "pw", 3001, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);

    /* Every account has a key, and it survives a save and a load. */
    pub = fl_user_db_signing_public(&db, "Root.Flinstone.Bob");
    ASSERT(pub);
    memcpy(before, pub, sizeof before);
    ASSERT(fl_user_db_load(&again, path) == FL_RESULT_OK);
    pub = fl_user_db_signing_public(&again, "Root.Flinstone.Bob");
    ASSERT(pub && memcmp(pub, before, sizeof before) == 0);
    ASSERT(fl_user_db_signing_public(&again, "Root"));
    ASSERT(fl_user_db_open_key(&db, "Bob", "wrong", bob) == FL_RESULT_ACCES);
    ASSERT(fl_user_db_open_key(&db, "Bob", "pw", bob) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(&db, "Ann", "pw", ann) == FL_RESULT_OK);

    /* Signing with someone else's key is refused. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 1, ann, &id)
           == FL_RESULT_ACCES);
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 1, bob, &id)
           == FL_RESULT_OK);
    ASSERT(fl_bridge_accept(path, &db, "Root.Alternative.Ann", id, 2, bob) == FL_RESULT_ACCES);
    ASSERT(fl_bridge_accept(path, &db, "Root.Alternative.Ann", id, 2, ann) == FL_RESULT_OK);
    ASSERT(fl_bridge_active_between(&db, "Root.Flinstone.Bob", "Root.Alternative.Ann"));

    /* A changed signature opens nothing. */
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE bridges SET accept_sig=request_sig;", NULL, NULL, NULL)
           == SQLITE_OK);
    ASSERT(!fl_bridge_active_between(&db, "Root.Flinstone.Bob", "Root.Alternative.Ann"));
    /* Nor does an active row with no signatures (one written before bridges were signed). */
    ASSERT(sqlite3_exec(sql, "DELETE FROM bridges;", NULL, NULL, NULL) == SQLITE_OK);
    snprintf(stmt, sizeof stmt,
             "INSERT INTO bridges(a_key,b_key,state,accepted_by) VALUES("
             "'Root.Flinstone.Bob','Root.Alternative.Ann',%d,'Root.Alternative.Ann');",
             (int)FL_BRIDGE_STATE_ACTIVE);
    ASSERT(sqlite3_exec(sql, stmt, NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(!fl_bridge_active_between(&db, "Root.Flinstone.Bob", "Root.Alternative.Ann"));
    ASSERT(sqlite3_exec(sql, "DELETE FROM bridges;", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);

    /* A request with no valid requester signature cannot be accepted. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 3, bob, &id)
           == FL_RESULT_OK);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE bridges SET request_sig='';", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_bridge_accept(path, &db, "Root.Alternative.Ann", id, 4, ann) == FL_RESULT_INVAL);
    ASSERT(fl_bridge_revoke(path, "Root.Flinstone.Bob", id) == FL_RESULT_OK);

    /* Changing your own password keeps the key; a reset replaces it and the bridge stops counting. */
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 5, bob, &id)
           == FL_RESULT_OK);
    ASSERT(fl_bridge_accept(path, &db, "Root.Alternative.Ann", id, 6, ann) == FL_RESULT_OK);
    memcpy(before, fl_user_db_signing_public(&db, "Root.Alternative.Ann"), sizeof before);
    ASSERT(fl_user_db_change_password(&db, "Ann", "wrong", "pw2") == FL_RESULT_ACCES);
    ASSERT(fl_user_db_change_password(&db, "Ann", "pw", "pw2") == FL_RESULT_OK);
    ASSERT(memcmp(fl_user_db_signing_public(&db, "Root.Alternative.Ann"), before, sizeof before) == 0);
    ASSERT(fl_user_db_open_key(&db, "Ann", "pw2", ann) == FL_RESULT_OK);
    ASSERT(fl_bridge_active_between(&db, "Root.Flinstone.Bob", "Root.Alternative.Ann"));
    ASSERT(fl_user_db_set_password(&db, "Ann", "pw3") == FL_RESULT_OK);
    ASSERT(memcmp(fl_user_db_signing_public(&db, "Root.Alternative.Ann"), before, sizeof before) != 0);
    ASSERT(!fl_bridge_active_between(&db, "Root.Flinstone.Bob", "Root.Alternative.Ann"));

    /* ensure_key leaves an existing key alone. */
    ASSERT(fl_user_db_ensure_key(&db, "Bob", "pw", &created) == FL_RESULT_OK && created == 0);
    /* A key that could not be saved is forgotten, and a later ensure_key makes a new one. */
    memcpy(before, fl_user_db_signing_public(&db, "Root.Flinstone.Bob"), sizeof before);
    fl_user_db_forget_key(&db, "Bob");
    ASSERT(!fl_user_db_signing_public(&db, "Root.Flinstone.Bob"));
    ASSERT(fl_user_db_open_key(&db, "Bob", "pw", bob) == FL_RESULT_NOENT);
    ASSERT(fl_user_db_ensure_key(&db, "Bob", "pw", &created) == FL_RESULT_OK &&
           created == FL_USER_DB_CREATED_KEYS);
    ASSERT(memcmp(fl_user_db_signing_public(&db, "Root.Flinstone.Bob"), before, sizeof before) != 0);

    mem_domain_zero(bob, sizeof bob);
    mem_domain_zero(ann, sizeof ann);
    unlink(path);
    return 0;
}


/* Box keys (what co-signed file letters are sealed to) follow the signing key. */
static int test_box_keys(void) {
    char path[256];
    fl_user_db_t db;
    fl_user_db_t again;
    sqlite3 *sql = NULL;
    int created = 0;
    unsigned char box[FL_USER_SIGNING_SECRET_LEN];
    unsigned char before[FL_USER_SIGNING_PUBLIC_LEN];
    const unsigned char *pub;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Flinstone", "Mya", "pw", 3100, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);

    /* A new account has a box key distinct from its signing key; it survives a reload. */
    pub = fl_user_db_box_public(&db, "Root.Flinstone.Mya");
    ASSERT(pub);
    ASSERT(memcmp(pub, fl_user_db_signing_public(&db, "Root.Flinstone.Mya"), 32) != 0);
    memcpy(before, pub, sizeof before);
    ASSERT(fl_user_db_load(&again, path) == FL_RESULT_OK);
    pub = fl_user_db_box_public(&again, "Root.Flinstone.Mya");
    ASSERT(pub && memcmp(pub, before, sizeof before) == 0);
    ASSERT(fl_user_db_open_box(&again, "Mya", "wrong", box) == FL_RESULT_ACCES);
    ASSERT(fl_user_db_open_box(&again, "Mya", "pw", box) == FL_RESULT_OK);

    /* A password change re-seals it; an admin reset replaces it. */
    ASSERT(fl_user_db_change_password(&again, "Mya", "pw", "pw2") == FL_RESULT_OK);
    ASSERT(memcmp(fl_user_db_box_public(&again, "Root.Flinstone.Mya"), before, sizeof before) == 0);
    ASSERT(fl_user_db_open_box(&again, "Mya", "pw", box) == FL_RESULT_ACCES);
    ASSERT(fl_user_db_open_box(&again, "Mya", "pw2", box) == FL_RESULT_OK);
    ASSERT(fl_user_db_set_password(&again, "Mya", "pw3") == FL_RESULT_OK);
    ASSERT(memcmp(fl_user_db_box_public(&again, "Root.Flinstone.Mya"), before, sizeof before) != 0);
    ASSERT(fl_user_db_save(&again) == FL_RESULT_OK);

    /* An account from before box keys keeps its signing key and gains a box key. */
    memcpy(before, fl_user_db_signing_public(&again, "Root.Flinstone.Mya"), sizeof before);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE users SET box_public='',box_sealed='' WHERE name='Mya';",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(!fl_user_db_box_public(&db, "Root.Flinstone.Mya"));
    ASSERT(fl_user_db_open_box(&db, "Mya", "pw3", box) == FL_RESULT_NOENT);
    ASSERT(fl_user_db_ensure_key(&db, "Mya", "pw3", &created) == FL_RESULT_OK &&
           created == FL_USER_DB_CREATED_BOX);
    ASSERT(fl_user_db_box_public(&db, "Root.Flinstone.Mya"));
    ASSERT(memcmp(fl_user_db_signing_public(&db, "Root.Flinstone.Mya"), before, sizeof before) == 0);
    /* A save that failed undoes only the new box key: the signing key, and what it signed, stay. */
    fl_user_db_forget_created(&db, "Mya", created);
    ASSERT(!fl_user_db_box_public(&db, "Root.Flinstone.Mya"));
    ASSERT(memcmp(fl_user_db_signing_public(&db, "Root.Flinstone.Mya"), before, sizeof before) == 0);
    ASSERT(fl_user_db_ensure_key(&db, "Mya", "pw3", &created) == FL_RESULT_OK &&
           created == FL_USER_DB_CREATED_BOX);
    ASSERT(memcmp(fl_user_db_signing_public(&db, "Root.Flinstone.Mya"), before, sizeof before) == 0);
    ASSERT(fl_user_db_ensure_key(&db, "Mya", "pw3", &created) == FL_RESULT_OK && created == 0);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);

    /* A damaged signing key beside a valid box key: a password change makes both keys new,
     * and the box key it stores is one the new password opens and that matches its public. */
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE users SET signing_public='',signing_sealed='' WHERE name='Mya';",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(!fl_user_db_signing_public(&db, "Root.Flinstone.Mya"));
    ASSERT(fl_user_db_box_public(&db, "Root.Flinstone.Mya"));
    ASSERT(fl_user_db_change_password(&db, "Mya", "pw3", "pw4") == FL_RESULT_OK);
    ASSERT(fl_user_db_signing_public(&db, "Root.Flinstone.Mya"));
    ASSERT(fl_user_db_open_box(&db, "Mya", "pw4", box) == FL_RESULT_OK);

    mem_domain_zero(box, sizeof box);
    unlink(path);
    return 0;
}

/* The sender-side gate for server file offers (share_gate.h). */
static int test_share_gate(void) {
    char path[256];
    fl_user_db_t db;
    int64_t id = 0;
    unsigned char bob[FL_USER_SIGNING_SECRET_LEN];
    unsigned char ann[FL_USER_SIGNING_SECRET_LEN];
    sqlite3 *sql = NULL;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Flinstone", "Bob", "pw", 3000, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root.Alternative", "Ann", "pw", 3001, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);

    /* One line: yourself, an ancestor, a descendant. */
    ASSERT(fl_share_gate_check(&db, "Bob", "Bob") == FL_SHARE_GATE_OK);
    ASSERT(fl_share_gate_check(&db, "Bob", "Flinstone") == FL_SHARE_GATE_OK);
    ASSERT(fl_share_gate_check(&db, "Flinstone", "Bob") == FL_SHARE_GATE_OK);
    ASSERT(fl_share_gate_check(&db, "Root", "Ann") == FL_SHARE_GATE_OK);
    /* Another branch needs a signed, active bridge. */
    ASSERT(fl_share_gate_check(&db, "Bob", "Ann") == FL_SHARE_GATE_NO_BRIDGE);
    ASSERT(fl_share_gate_check(&db, "Flinstone", "Alternative") == FL_SHARE_GATE_NO_BRIDGE);
    ASSERT(fl_user_db_open_key(&db, "Bob", "pw", bob) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(&db, "Ann", "pw", ann) == FL_RESULT_OK);
    ASSERT(fl_bridge_request(path, &db, "Root.Flinstone.Bob", "Root.Alternative.Ann", 1, bob, &id)
           == FL_RESULT_OK);
    ASSERT(fl_share_gate_check(&db, "Bob", "Ann") == FL_SHARE_GATE_NO_BRIDGE); /* pending */
    ASSERT(fl_bridge_accept(path, &db, "Root.Alternative.Ann", id, 2, ann) == FL_RESULT_OK);
    ASSERT(fl_share_gate_check(&db, "Bob", "Ann") == FL_SHARE_GATE_OK);
    ASSERT(fl_share_gate_check(&db, "Ann", "Bob") == FL_SHARE_GATE_OK);
    /* The bridge joins exactly those two: Bob's parent does not inherit it. */
    ASSERT(fl_share_gate_check(&db, "Flinstone", "Ann") == FL_SHARE_GATE_NO_BRIDGE);
    /* A tampered bridge stops counting. */
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE bridges SET accept_sig=request_sig;", NULL, NULL, NULL)
           == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_share_gate_check(&db, "Bob", "Ann") == FL_SHARE_GATE_NO_BRIDGE);

    /* Unknown accounts and accounts without a key are refused. */
    ASSERT(fl_share_gate_check(&db, "Bob", "Ghost") == FL_SHARE_GATE_NO_RECEIVER);
    ASSERT(fl_share_gate_check(&db, "Ghost", "Bob") == FL_SHARE_GATE_NO_SENDER);
    {
        size_t i;

        for (i = 0; i < db.count; i++) {
            if (!strcmp(db.users[i].name, "Bob"))
                db.users[i].has_key = 0;
        }
    }
    ASSERT(fl_share_gate_check(&db, "Flinstone", "Bob") == FL_SHARE_GATE_NO_RECEIVER_KEY);

    mem_domain_zero(bob, sizeof bob);
    mem_domain_zero(ann, sizeof ann);
    unlink(path);
    return 0;
}

#define AUTH_SNAP_MAX 8

typedef struct auth_row_snap {
    char name[FL_USER_NAME_MAX];
    char hash[FL_PASSWORD_HASH_HEX_MAX];
    char salt[FL_PASSWORD_SALT_HEX_MAX];
    int sql_type;
    sqlite3_int64 ival;
    double rval;
    char text[64];
    unsigned char blob[16];
    int blob_len;
} auth_row_snap;

static int auth_snap_rows(const char *path, auth_row_snap *rows, int cap, int *nout) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    int n = 0;

    if (sqlite3_open(path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    if (sqlite3_prepare_v2(sql,
                           "SELECT name, password_hash, salt_hex, auth_mode FROM users ORDER BY uid;",
                           -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        const char *hash = (const char *)sqlite3_column_text(st, 1);
        const char *salt = (const char *)sqlite3_column_text(st, 2);
        int t;

        if (n >= cap || !name || !hash || !salt) {
            sqlite3_finalize(st);
            sqlite3_close(sql);
            return -1;
        }
        memset(&rows[n], 0, sizeof rows[n]);
        strncpy(rows[n].name, name, sizeof rows[n].name - 1);
        strncpy(rows[n].hash, hash, sizeof rows[n].hash - 1);
        strncpy(rows[n].salt, salt, sizeof rows[n].salt - 1);
        t = sqlite3_column_type(st, 3);
        rows[n].sql_type = t;
        if (t == SQLITE_INTEGER)
            rows[n].ival = sqlite3_column_int64(st, 3);
        else if (t == SQLITE_FLOAT)
            rows[n].rval = sqlite3_column_double(st, 3);
        else if (t == SQLITE_TEXT) {
            const char *tx = (const char *)sqlite3_column_text(st, 3);

            if (tx)
                strncpy(rows[n].text, tx, sizeof rows[n].text - 1);
        } else if (t == SQLITE_BLOB) {
            int nbytes = sqlite3_column_bytes(st, 3);
            const void *blob = sqlite3_column_blob(st, 3);
            int copy = nbytes;

            rows[n].blob_len = nbytes;
            if (blob && copy > 0) {
                if (copy > (int)sizeof rows[n].blob)
                    copy = (int)sizeof rows[n].blob;
                memcpy(rows[n].blob, blob, (size_t)copy);
            }
        }
        n++;
    }
    sqlite3_finalize(st);
    sqlite3_close(sql);
    *nout = n;
    return 0;
}

static int auth_snaps_equal(const auth_row_snap *a, const auth_row_snap *b, int n) {
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(a[i].name, b[i].name) != 0 || strcmp(a[i].hash, b[i].hash) != 0 ||
            strcmp(a[i].salt, b[i].salt) != 0 || a[i].sql_type != b[i].sql_type)
            return 0;
        if (a[i].sql_type == SQLITE_INTEGER && a[i].ival != b[i].ival)
            return 0;
        if (a[i].sql_type == SQLITE_FLOAT && a[i].rval != b[i].rval)
            return 0;
        if (a[i].sql_type == SQLITE_TEXT && strcmp(a[i].text, b[i].text) != 0)
            return 0;
        if (a[i].sql_type == SQLITE_BLOB &&
            (a[i].blob_len != b[i].blob_len || memcmp(a[i].blob, b[i].blob, sizeof a[i].blob) != 0))
            return 0;
    }
    return 1;
}

/* sql_type is the storage class that must still be on disk. ival is used for INTEGER,
 * text for TEXT. FLOAT and BLOB expectations are the fixtures 1.5 and x'01'. */
static int auth_cell_is(const char *path, const char *name, int sql_type, sqlite3_int64 ival,
                        const char *text) {
    auth_row_snap rows[AUTH_SNAP_MAX];
    int n = 0;
    int i;

    if (auth_snap_rows(path, rows, AUTH_SNAP_MAX, &n) != 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (strcmp(rows[i].name, name) != 0)
            continue;
        if (rows[i].sql_type != sql_type)
            return 0;
        if (sql_type == SQLITE_INTEGER && rows[i].ival != ival)
            return 0;
        if (sql_type == SQLITE_TEXT && (!text || strcmp(rows[i].text, text) != 0))
            return 0;
        if (sql_type == SQLITE_FLOAT && rows[i].rval != 1.5)
            return 0;
        if (sql_type == SQLITE_BLOB && (rows[i].blob_len != 1 || rows[i].blob[0] != 0x01))
            return 0;
        return 1;
    }
    return 0;
}

static int auth_exec_sql(const char *path, const char *sql_text) {
    sqlite3 *sql = NULL;
    int rc;

    if (sqlite3_open(path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    rc = sqlite3_exec(sql, sql_text, NULL, NULL, NULL);
    sqlite3_close(sql);
    return rc == SQLITE_OK ? 0 : -1;
}

/* A bad stored mode publishes no users and does not rewrite the file. */
static int auth_expect_load_err(const char *path) {
    fl_user_db_t db;
    auth_row_snap before[AUTH_SNAP_MAX], after[AUTH_SNAP_MAX];
    int nb = 0;
    int na = 0;
    int i;

    if (auth_snap_rows(path, before, AUTH_SNAP_MAX, &nb) != 0 || nb < 1)
        return 1;
    if (fl_user_db_load(&db, path) != FL_RESULT_ERR)
        return 1;
    if (db.loaded != 0 || db.count != 0 || strcmp(db.path, path) != 0)
        return 1;
    for (i = 0; i < nb; i++) {
        if (fl_user_db_find(&db, before[i].name) != NULL)
            return 1;
    }
    if (auth_snap_rows(path, after, AUTH_SNAP_MAX, &na) != 0 || na != nb)
        return 1;
    if (!auth_snaps_equal(before, after, nb))
        return 1;
    return 0;
}

static int auth_write_null_mode(const char *path) {
    sqlite3 *sql = NULL;
    int rc;
    const char *stmt =
        "CREATE TABLE users ("
        " name TEXT PRIMARY KEY NOT NULL,"
        " password_hash TEXT NOT NULL,"
        " salt_hex TEXT NOT NULL,"
        " uid INTEGER NOT NULL,"
        " is_elevated INTEGER NOT NULL DEFAULT 0,"
        " key_id TEXT NOT NULL DEFAULT '',"
        " created_by TEXT NOT NULL DEFAULT '',"
        " created_at INTEGER NOT NULL DEFAULT 0,"
        " signing_public TEXT NOT NULL DEFAULT '',"
        " signing_sealed TEXT NOT NULL DEFAULT '',"
        " box_public TEXT NOT NULL DEFAULT '',"
        " box_sealed TEXT NOT NULL DEFAULT '',"
        " auth_mode INTEGER);"
        "INSERT INTO users (name,password_hash,salt_hex,uid,key_id,auth_mode)"
        " VALUES ('Root','x','y',0,'Root',NULL);";

    if (sqlite3_open(path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    rc = sqlite3_exec(sql, stmt, NULL, NULL, NULL);
    sqlite3_close(sql);
    return rc == SQLITE_OK ? 0 : -1;
}

/* Valid modes load. Unknown integers, a non-integer storage class, a partial row, and a
 * load through db->path either publish the whole store or none, and they do not rewrite
 * the file. Does not call fl_session_init. */
static int test_auth_mode_reject_malformed(void) {
    char path[256], partial[256], alias[256], badalias[256], nulls[256], longp[257];
    fl_user_db_t db;
    const fl_user_record_t *u;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_set_auth_mode(&db, "Root", FL_USER_AUTH_PASSWORD) == FL_RESULT_OK);
    ASSERT(fl_user_db_set_auth_mode(&db, "Flinstone", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(fl_user_db_save(&db) == FL_RESULT_OK);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    u = fl_user_db_find(&db, "Root");
    ASSERT(u && u->auth_mode == FL_USER_AUTH_PASSWORD);
    u = fl_user_db_find(&db, "Flinstone");
    ASSERT(u && u->auth_mode == FL_USER_AUTH_PASSWORD_KQST);

    /* Bad API arguments do not publish or clear a store that already loaded. */
    ASSERT(fl_user_db_load(&db, "") == FL_RESULT_INVAL);
    ASSERT(db.loaded == 1 && fl_user_db_find(&db, "Root") && strcmp(db.path, path) == 0);
    ASSERT(fl_user_db_load(&db, NULL) == FL_RESULT_INVAL);
    ASSERT(db.loaded == 1 && strcmp(db.path, path) == 0);
    ASSERT(fl_user_db_load(NULL, path) == FL_RESULT_INVAL);
    memset(longp, 'b', sizeof longp - 1);
    longp[sizeof longp - 1] = '\0';
    ASSERT(strlen(longp) >= sizeof db.path);
    ASSERT(fl_user_db_load(&db, longp) == FL_RESULT_INVAL);
    ASSERT(db.loaded == 1 && strcmp(db.path, path) == 0);

    ASSERT(auth_exec_sql(path, "UPDATE users SET auth_mode=2 WHERE name='Root';") == 0);
    ASSERT(auth_cell_is(path, "Root", SQLITE_INTEGER, 2, NULL));
    ASSERT(auth_expect_load_err(path) == 0);
    ASSERT(auth_exec_sql(path, "UPDATE users SET auth_mode=-1 WHERE name='Root';") == 0);
    ASSERT(auth_cell_is(path, "Root", SQLITE_INTEGER, -1, NULL));
    ASSERT(auth_expect_load_err(path) == 0);
    ASSERT(auth_exec_sql(path, "UPDATE users SET auth_mode=4294967296 WHERE name='Root';") == 0);
    ASSERT(auth_cell_is(path, "Root", SQLITE_INTEGER, (sqlite3_int64)4294967296LL, NULL));
    ASSERT(auth_expect_load_err(path) == 0);
    ASSERT(auth_exec_sql(path, "UPDATE users SET auth_mode='nope' WHERE name='Root';") == 0);
    ASSERT(auth_cell_is(path, "Root", SQLITE_TEXT, 0, "nope"));
    ASSERT(auth_expect_load_err(path) == 0);
    ASSERT(auth_exec_sql(path, "UPDATE users SET auth_mode=1.5 WHERE name='Root';") == 0);
    ASSERT(auth_cell_is(path, "Root", SQLITE_FLOAT, 0, NULL));
    ASSERT(auth_expect_load_err(path) == 0);
    ASSERT(auth_exec_sql(path, "UPDATE users SET auth_mode=x'01' WHERE name='Root';") == 0);
    ASSERT(auth_cell_is(path, "Root", SQLITE_BLOB, 0, NULL));
    ASSERT(auth_expect_load_err(path) == 0);
    unlink(path);

    ASSERT(temp_db(partial, sizeof partial) == 0);
    ASSERT(fl_user_db_load(&db, partial) == FL_RESULT_OK);
    ASSERT(auth_exec_sql(partial, "UPDATE users SET auth_mode=2 WHERE name='Flinstone';") == 0);
    ASSERT(auth_cell_is(partial, "Root", SQLITE_INTEGER, 0, NULL));
    ASSERT(auth_cell_is(partial, "Flinstone", SQLITE_INTEGER, 2, NULL));
    ASSERT(auth_expect_load_err(partial) == 0);
    unlink(partial);

    /* fl_user_db_load(db, db.path) must copy the path before clear. */
    ASSERT(temp_db(alias, sizeof alias) == 0);
    ASSERT(fl_user_db_load(&db, alias) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root", "Temp", "pw", 4100, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_find(&db, "Temp") != NULL);
    ASSERT(fl_user_db_load(&db, db.path) == FL_RESULT_OK);
    ASSERT(fl_user_db_find(&db, "Temp") == NULL);
    ASSERT(fl_user_db_find(&db, "Root") != NULL);
    ASSERT(fl_user_db_find(&db, "Flinstone") != NULL);
    ASSERT(strcmp(db.path, alias) == 0);
    unlink(alias);

    ASSERT(temp_db(badalias, sizeof badalias) == 0);
    ASSERT(fl_user_db_load(&db, badalias) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&db, "Root", "Temp", "pw", 4100, 0, 0) == FL_RESULT_OK);
    ASSERT(auth_exec_sql(badalias, "UPDATE users SET auth_mode=2 WHERE name='Root';") == 0);
    ASSERT(auth_cell_is(badalias, "Root", SQLITE_INTEGER, 2, NULL));
    {
        auth_row_snap before[AUTH_SNAP_MAX], after[AUTH_SNAP_MAX];
        int nb = 0;
        int na = 0;
        int i;

        ASSERT(auth_snap_rows(badalias, before, AUTH_SNAP_MAX, &nb) == 0 && nb >= 2);
        ASSERT(fl_user_db_load(&db, db.path) == FL_RESULT_ERR);
        ASSERT(db.loaded == 0 && db.count == 0);
        ASSERT(strcmp(db.path, badalias) == 0);
        ASSERT(fl_user_db_find(&db, "Temp") == NULL);
        ASSERT(fl_user_db_find(&db, "Root") == NULL);
        ASSERT(auth_snap_rows(badalias, after, AUTH_SNAP_MAX, &na) == 0 && na == nb);
        ASSERT(auth_snaps_equal(before, after, nb));
        for (i = 0; i < na; i++)
            ASSERT(strcmp(after[i].name, "Temp") != 0);
    }
    unlink(badalias);

    ASSERT(temp_db(nulls, sizeof nulls) == 0);
    ASSERT(auth_write_null_mode(nulls) == 0);
    ASSERT(auth_cell_is(nulls, "Root", SQLITE_NULL, 0, NULL));
    ASSERT(auth_expect_load_err(nulls) == 0);
    unlink(nulls);
    return 0;
}

/* Runs only in a child, before that process's fl_session_init. */
static int auth_startup_child(const char *path, const char *lab) {
    fl_session_switch_request_t req;
    fl_result_t login_rc;
    fl_result_t switch_rc;

    if (setenv("FL_USERS_DB_PATH", path, 1) != 0 || setenv("FL_USERS_LAB_DEFAULTS", lab, 1) != 0)
        return 2;
    login_rc = fl_session_login("Root", "root");
    if (login_rc != FL_RESULT_ERR) {
        fprintf(stderr, "startup child lab=%s login returned %d\n", lab, (int)login_rc);
        return 3;
    }
    memset(&req, 0, sizeof req);
    req.target = "Root";
    req.sudo = 1;
    req.password = "root";
    switch_rc = fl_session_switch_user(&req);
    if (switch_rc == FL_RESULT_OK) {
        fprintf(stderr, "startup child lab=%s switch returned OK\n", lab);
        return 4;
    }
    return 0;
}

/* A bad auth_mode at startup must not seed over the file for either lab-default setting.
 * Forks before this process calls fl_session_init. */
static int test_auth_mode_startup_fail_closed(void) {
    char path[256];
    fl_user_db_t db;
    auth_row_snap before[AUTH_SNAP_MAX], after[AUTH_SNAP_MAX];
    int nb = 0;
    int na = 0;
    int i;
    const char *labs[2] = {"0", "1"};

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_find(&db, "Root") && fl_user_db_find(&db, "Flinstone") &&
           fl_user_db_find(&db, "Alternative"));
    ASSERT(auth_exec_sql(path, "UPDATE users SET auth_mode=2 WHERE name='Root';") == 0);
    ASSERT(auth_snap_rows(path, before, AUTH_SNAP_MAX, &nb) == 0);
    ASSERT(nb == 3);
    ASSERT(auth_cell_is(path, "Root", SQLITE_INTEGER, 2, NULL));
    for (i = 0; i < 2; i++) {
        pid_t pid;
        int status = 0;

        pid = fork();
        ASSERT(pid >= 0);
        if (pid == 0)
            _exit(auth_startup_child(path, labs[i]));
        ASSERT(waitpid(pid, &status, 0) == pid);
        ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        ASSERT(auth_snap_rows(path, after, AUTH_SNAP_MAX, &na) == 0);
        ASSERT(na == nb && auth_snaps_equal(before, after, nb));
        ASSERT(auth_cell_is(path, "Root", SQLITE_INTEGER, 2, NULL));
    }
    unlink(path);
    return 0;
}

/* Account homes in an active VM jail, through the session singleton. */
static int test_jail_homes(const char *jail_parent) {
    char home[PATH_MAX];
    char p[PATH_MAX];
    fl_user_db_t *db;
    int64_t id = 0;
    unsigned char bob[FL_USER_SIGNING_SECRET_LEN];
    unsigned char alt[FL_USER_SIGNING_SECRET_LEN];

    g_vm_mode = 1;
    strncpy(g_vm_root, jail_parent, sizeof(g_vm_root) - 1);
    fs_jail_init();
    ASSERT(fs_jail_is_active());

    fl_session_init();
    db = fl_session_user_db_mut();
    ASSERT(fl_session_add_user_under("Root.Flinstone", "Bob", "pw", 3000, 0, 0) == FL_RESULT_OK);
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
    ASSERT(fl_user_db_open_key(db, "Bob", "pw", bob) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(db, "Alternative", "alternative", alt) == FL_RESULT_OK);
    ASSERT(fl_bridge_request(db->path, db, "Root.Flinstone.Bob", "Root.Alternative", 1, bob, &id)
           == FL_RESULT_OK);
    ASSERT(fs_jail_check_access(p) == -1); /* pending */
    ASSERT(fl_bridge_accept(db->path, db, "Root.Alternative", id, 2, alt) == FL_RESULT_OK);
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

static void make_bridge(fl_user_bridge_t *b, const char *uid, const char *a, const char *z) {
    memset(b, 0, sizeof *b);
    snprintf(b->uid, sizeof b->uid, "%s", uid);
    b->generation = 3;
    memset(b->salt, 0x5a, sizeof b->salt);
    memset(b->public_key, 0x6b, sizeof b->public_key);
    b->nmembers = 2;
    snprintf(b->members[0].label, sizeof b->members[0].label, "%s", a);
    snprintf(b->members[1].label, sizeof b->members[1].label, "%s", z);
    memset(b->members[0].encryption_public, 0x01, 32);
    memset(b->members[0].signing_public, 0x02, 32);
    memset(b->members[1].encryption_public, 0x03, 32);
    memset(b->members[1].signing_public, 0x04, 32);
}

/* Private bridges in the account store (FL-12): a store from before wrap_salt gains the column, the
 * sealed key and its wrap salt are stored and read only together, and only rows the store holds as
 * live (destroyed = 0, well formed) are live. */
static int test_private_bridge_store(void) {
    char path[256];
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_user_db_t db;
    fl_user_bridge_t b, got, live[FL_USER_BRIDGES_MAX];
    unsigned char sealed[96], salt[16], out[FL_USER_BRIDGE_SEALED_MAX], out_salt[16];
    size_t n = 0, len = 0;
    int has_wrap = 0;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql,
                        "CREATE TABLE account_bridge_members (uid TEXT NOT NULL, label TEXT NOT NULL,"
                        " encryption_public BLOB NOT NULL, signing_public BLOB NOT NULL,"
                        " sealed_bridge_key BLOB, PRIMARY KEY (uid, label));",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_load(&db, path) == FL_RESULT_OK);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_prepare_v2(sql, "SELECT name FROM pragma_table_info('account_bridge_members');", -1,
                              &st, NULL) == SQLITE_OK);
    while (sqlite3_step(st) == SQLITE_ROW)
        if (!strcmp((const char *)sqlite3_column_text(st, 0), "wrap_salt"))
            has_wrap = 1;
    sqlite3_finalize(st);
    sqlite3_close(sql);
    ASSERT(has_wrap); /* the upgrade added it in place */

    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_OK && n == 0);
    make_bridge(&b, "aaaa0000aaaa0000aaaa0000aaaa0000", "Root.Flinstone.Bob", "Root.Alternative");
    ASSERT(fl_user_db_bridge_put(&db, &b) == FL_RESULT_OK);
    ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_OK);
    ASSERT(got.generation == 3 && got.nmembers == 2 && !got.destroyed);
    ASSERT(got.salt[0] == 0x5a && got.public_key[31] == 0x6b);
    ASSERT(!strcmp(got.members[0].label, "Root.Alternative")); /* roster in label order */
    ASSERT(got.members[1].signing_public[0] == 0x02);
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_OK && n == 1);
    ASSERT(!strcmp(live[0].uid, b.uid));
    ASSERT(fl_user_db_bridges_live(&db, live, 0, &n) == FL_RESULT_NOMEM && n == 0); /* never partial */

    /* A generation that is not an integer in 1..UINT32_MAX is not a live bridge. */
    {
        static const char *bad_gen[] = {
            "UPDATE account_bridges SET generation=1.5 WHERE uid='aaaa0000aaaa0000aaaa0000aaaa0000';",
            "UPDATE account_bridges SET generation='gen' WHERE uid='aaaa0000aaaa0000aaaa0000aaaa0000';",
            "UPDATE account_bridges SET generation=0 WHERE uid='aaaa0000aaaa0000aaaa0000aaaa0000';",
            "UPDATE account_bridges SET generation=4294967296 WHERE uid='aaaa0000aaaa0000aaaa0000aaaa0000';",
        };
        size_t gi;

        for (gi = 0; gi < sizeof bad_gen / sizeof bad_gen[0]; gi++) {
            ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
            ASSERT(sqlite3_exec(sql, bad_gen[gi], NULL, NULL, NULL) == SQLITE_OK);
            sqlite3_close(sql);
            ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_NOENT);
            ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_ERR && n == 0);
        }
        /* Numeric text stays text only without integer affinity. column_int64 would turn '3' into 3. */
        ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
        ASSERT(sqlite3_exec(sql,
                            "CREATE TABLE account_bridges_text (uid TEXT PRIMARY KEY NOT NULL,"
                            " generation TEXT, salt BLOB NOT NULL, public_key BLOB NOT NULL,"
                            " destroyed INTEGER NOT NULL DEFAULT 0);"
                            "INSERT INTO account_bridges_text SELECT uid, '3', salt, public_key, destroyed"
                            " FROM account_bridges;"
                            "DROP TABLE account_bridges;"
                            "ALTER TABLE account_bridges_text RENAME TO account_bridges;",
                            NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(sql);
        ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_NOENT);
        ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_ERR && n == 0);
        /* The column is NOT NULL, so a NULL generation has to be written behind that constraint. */
        ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
        ASSERT(sqlite3_exec(sql,
                            "CREATE TABLE account_bridges_null (uid TEXT PRIMARY KEY NOT NULL,"
                            " generation INTEGER, salt BLOB NOT NULL, public_key BLOB NOT NULL,"
                            " destroyed INTEGER NOT NULL DEFAULT 0);"
                            "INSERT INTO account_bridges_null SELECT uid, NULL, salt, public_key, destroyed"
                            " FROM account_bridges;"
                            "DROP TABLE account_bridges;"
                            "ALTER TABLE account_bridges_null RENAME TO account_bridges;",
                            NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(sql);
        ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_NOENT);
        ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_ERR && n == 0);
        ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
        ASSERT(sqlite3_exec(sql,
                            "CREATE TABLE account_bridges_ok (uid TEXT PRIMARY KEY NOT NULL,"
                            " generation INTEGER NOT NULL, salt BLOB NOT NULL, public_key BLOB NOT NULL,"
                            " destroyed INTEGER NOT NULL DEFAULT 0);"
                            "INSERT INTO account_bridges_ok SELECT uid, 3, salt, public_key, destroyed"
                            " FROM account_bridges;"
                            "DROP TABLE account_bridges;"
                            "ALTER TABLE account_bridges_ok RENAME TO account_bridges;",
                            NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(sql);
        ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_OK && got.generation == 3);
        ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_OK && n == 1);
    }

    /* The sealed key and its salt: both or nothing. */
    memset(sealed, 0x77, sizeof sealed);
    memset(salt, 0x88, sizeof salt);
    ASSERT(fl_user_db_bridge_sealed_key(&db, b.uid, "Root.Flinstone.Bob", out, sizeof out, &len, out_salt) ==
           FL_RESULT_NOENT);
    ASSERT(fl_user_db_bridge_put_sealed_key(&db, b.uid, "Root.Nobody", sealed, sizeof sealed, salt) ==
           FL_RESULT_NOENT);
    ASSERT(fl_user_db_bridge_put_sealed_key(&db, b.uid, "Root.Flinstone.Bob", sealed, sizeof sealed, salt) ==
           FL_RESULT_OK);
    ASSERT(fl_user_db_bridge_sealed_key(&db, b.uid, "Root.Flinstone.Bob", out, sizeof out, &len, out_salt) ==
           FL_RESULT_OK);
    ASSERT(len == sizeof sealed && out[95] == 0x77 && out_salt[15] == 0x88);
    ASSERT(fl_user_db_bridge_sealed_key(&db, b.uid, "Root.Flinstone.Bob", out, 8, &len, out_salt) ==
           FL_RESULT_NOMEM);
    /* A sealed key without its salt (written behind the API) opens nothing. */
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE account_bridge_members SET sealed_bridge_key=x'99', wrap_salt=NULL"
                             " WHERE label='Root.Alternative';", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_bridge_sealed_key(&db, b.uid, "Root.Alternative", out, sizeof out, &len, out_salt) ==
           FL_RESULT_NOENT);

    /* Re-putting the roster drops a removed member and its sealed key. */
    make_bridge(&b, "aaaa0000aaaa0000aaaa0000aaaa0000", "Root.Flinstone.Bob", "Root.Alternative.Ann");
    b.generation = 4;
    ASSERT(fl_user_db_bridge_put(&db, &b) == FL_RESULT_OK);
    ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_OK && got.generation == 4);
    ASSERT(fl_user_db_bridge_sealed_key(&db, b.uid, "Root.Flinstone.Bob", out, sizeof out, &len, out_salt) ==
           FL_RESULT_NOENT);

    /* Destroyed is not live, and neither is a malformed row. */
    b.destroyed = 1;
    ASSERT(fl_user_db_bridge_put(&db, &b) == FL_RESULT_OK);
    ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_OK && got.destroyed == 1);
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_OK && n == 0);
    make_bridge(&b, "bbbb0000bbbb0000bbbb0000bbbb0000", "Root.Flinstone.Bob", "Root.Alternative");
    ASSERT(fl_user_db_bridge_put(&db, &b) == FL_RESULT_OK);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE account_bridges SET salt=x'00' WHERE uid='bbbb0000bbbb0000bbbb0000bbbb0000';",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_NOENT);
    /* A live row the store cannot read whole is no list at all (never a bridge left out). */
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_ERR && n == 0);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE account_bridge_members SET signing_public=x'00' WHERE uid="
                             "'bbbb0000bbbb0000bbbb0000bbbb0000' AND label='Root.Alternative';"
                             "UPDATE account_bridges SET salt=zeroblob(16) WHERE uid='bbbb0000bbbb0000bbbb0000bbbb0000';",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_ERR && n == 0);
    /* A malformed row of a destroyed bridge is not live: it does not stop the list. */
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "UPDATE account_bridges SET destroyed=1 WHERE uid='bbbb0000bbbb0000bbbb0000bbbb0000';",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_OK && n == 0);
    /* A store that cannot be read is an error, not "no bridge": locked by another writer... */
    make_bridge(&b, "cccc1111cccc1111cccc1111cccc1111", "Root.Flinstone.Bob", "Root.Alternative");
    ASSERT(fl_user_db_bridge_put(&db, &b) == FL_RESULT_OK);
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_OK && n == 1);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "BEGIN EXCLUSIVE;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_ERR && n == 0);
    ASSERT(fl_user_db_bridge_get(&db, b.uid, &got) == FL_RESULT_ERR);
    ASSERT(sqlite3_exec(sql, "ROLLBACK;", NULL, NULL, NULL) == SQLITE_OK);
    /* ...or with a column the query needs gone (the list cannot even be asked for). */
    ASSERT(sqlite3_exec(sql, "ALTER TABLE account_bridges DROP COLUMN destroyed;", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(sql);
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_ERR && n == 0);
    /* A one-member roster is no bridge. */
    b.nmembers = 1;
    ASSERT(fl_user_db_bridge_put(&db, &b) == FL_RESULT_INVAL);
    unlink(path);

    /* A store written before the bridge tables existed has no bridge: that alone is "none". */
    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(sqlite3_open(path, &sql) == SQLITE_OK);
    ASSERT(sqlite3_exec(sql, "CREATE TABLE users (name TEXT PRIMARY KEY NOT NULL);", NULL, NULL, NULL) ==
           SQLITE_OK);
    sqlite3_close(sql);
    memset(&db, 0, sizeof db);
    snprintf(db.path, sizeof db.path, "%s", path);
    ASSERT(fl_user_db_bridges_live(&db, live, FL_USER_BRIDGES_MAX, &n) == FL_RESULT_OK && n == 0);
    unlink(path);
    return 0;
}

/* FL-12: the switch factor is KeyQuorum's (kq_access_requirement), asked with heritage key ids and
 * the bridges the session's store holds as live. Any error is REFUSED. */
static int test_unlock_requirement(void) {
    fl_user_db_t *db = fl_session_user_db_mut();
    fl_user_bridge_t b;
    fl_kq_requirement_t r;

    ASSERT(fl_user_db_find(db, "Bob") && fl_user_db_find(db, "Alternative"));
    r = fl_kq_unlock_requirement("Flinstone", "Flinstone", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_NONE && r.bridge_index == -1 && !r.needs_parent_grant);
    r = fl_kq_unlock_requirement("Flinstone", "Flinstone", 1);
    ASSERT(r.factor == FL_KQ_ACCESS_KQST_PASSWORD); /* sudo: Root's token */
    r = fl_kq_unlock_requirement("flinstone", "root", 0); /* names are case-insensitive, key ids are not */
    ASSERT(r.factor == FL_KQ_ACCESS_KQST_PASSWORD);
    r = fl_kq_unlock_requirement("Flinstone", "Bob", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_PASSWORD && !r.needs_parent_grant);
    r = fl_kq_unlock_requirement("Bob", "Alternative", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_KQST_PASSWORD && r.bridge_uid[0] == '\0');
    r = fl_kq_unlock_requirement("Bob", "Nobody", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_REFUSED);
    r = fl_kq_unlock_requirement(NULL, "Bob", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_REFUSED);

    /* A live private bridge Bob <-> Alternative changes the cross-branch factor. */
    make_bridge(&b, "cccc0000cccc0000cccc0000cccc0000", "Root.Flinstone.Bob", "Root.Alternative");
    ASSERT(fl_user_db_bridge_put(db, &b) == FL_RESULT_OK);
    r = fl_kq_unlock_requirement("Bob", "Alternative", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST && r.bridge_index == 0);
    ASSERT(!strcmp(r.bridge_uid, b.uid));
    /* Proving needs a sealed key the store holds and a token that opens: neither is here. */
    ASSERT(fl_kq_prove_bridge_member(b.uid, "Bob", "pw", NULL) == FL_KQ_BRIDGE_REFUSED);
    ASSERT(fl_kq_prove_bridge_member(b.uid, "Flinstone", "flinstone", NULL) == FL_KQ_BRIDGE_REFUSED);
    ASSERT(fl_kq_prove_bridge_member("dddd0000dddd0000dddd0000dddd0000", "Bob", "pw", NULL) == FL_KQ_BRIDGE_STALE);
    /* Destroyed: the bridge is not live, so not offered, and a proof is stale. */
    b.destroyed = 1;
    ASSERT(fl_user_db_bridge_put(db, &b) == FL_RESULT_OK);
    r = fl_kq_unlock_requirement("Bob", "Alternative", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_KQST_PASSWORD && r.bridge_index == -1);
    ASSERT(fl_kq_prove_bridge_member(b.uid, "Bob", "pw", NULL) == FL_KQ_BRIDGE_STALE);
    return 0;
}

static char s_audit[96];

static void capture_audit(const char *actor, const char *event, const char *subject, const char *detail) {
    (void)actor;
    (void)subject;
    snprintf(s_audit, sizeof s_audit, "%s:%s", event ? event : "", detail ? detail : "");
}

static fl_result_t switch_to(const char *target, int sudo, const char *pw, const char *member,
                             const char *member_pw) {
    fl_session_switch_request_t req = {target, sudo, pw, member, member_pw};

    s_audit[0] = '\0';
    return fl_session_switch_user(&req);
}

/* The in-memory elevation mark, for the sudo -i waiver recheck. Root's uid stays 0. */
static void drop_root_elevation(void) { (void)fl_session_set_elevated("Root", 0); }

static fl_result_t restore_root_elevation(void) { return fl_session_set_elevated("Root", 1); }

static void sudo_logout_during(void) { (void)fl_session_logout(); }

static void sudo_root_kqst_during(void) {
    (void)fl_session_set_auth_mode("Root", FL_USER_AUTH_PASSWORD_KQST);
}

static void sudo_caller_password_during(void) { (void)fl_session_set_password("Flinstone", "other"); }

/* FL-12: su and sudo -i commit only through fl_session_switch_user, which asks KeyQuorum for the
 * factor itself. Password-only accounts keep their behaviour where the rule asks for a token they do
 * not have (owner decision, audited); a KQST-mode account never switches on a password alone. */
static int test_session_switch(void) {
    fl_user_db_t *db = fl_session_user_db_mut();
    fl_user_bridge_t b;

    fl_session_set_audit_hook(capture_audit);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    ASSERT(switch_to("Nobody", 0, "x", NULL, NULL) == FL_RESULT_NOENT);
    ASSERT(switch_to(NULL, 0, "x", NULL, NULL) == FL_RESULT_INVAL);
    /* Same branch (PASSWORD): the target's password. */
    ASSERT(switch_to("Bob", 0, "wrong", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:password"));
    ASSERT(!strcmp(fl_session_current_user(), "Flinstone"));
    ASSERT(switch_to("Bob", 0, NULL, NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_OK);
    ASSERT(!strcmp(fl_session_current_user(), "Bob") && s_audit[0] == '\0');
    /* Same account (NONE). */
    ASSERT(switch_to("Bob", 0, NULL, NULL, NULL) == FL_RESULT_OK);
    /* Across branches, no bridge (KQST_PASSWORD) to a password-only account: its password, audited. */
    ASSERT(switch_to("Alternative", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(switch_to("Alternative", 0, "alternative", NULL, NULL) == FL_RESULT_OK);
    ASSERT(!strcmp(s_audit, "su-kqst-legacy:password-only account"));
    /* To a KQST-mode account: the password alone is refused (no token is enrolled for Bob). */
    ASSERT(fl_session_set_auth_mode("Bob", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:kqst"));
    ASSERT(!strcmp(fl_session_current_user(), "Alternative"));
    ASSERT(fl_session_set_auth_mode("Bob", FL_USER_AUTH_PASSWORD) == FL_RESULT_OK);
    /* A live private bridge: the target's password and a member's proof, which needs a token. */
    make_bridge(&b, "eeee0000eeee0000eeee0000eeee0000", "Root.Flinstone.Bob", "Root.Alternative");
    ASSERT(fl_user_db_bridge_put(db, &b) == FL_RESULT_OK);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:bridge-member"));
    ASSERT(switch_to("Bob", 0, "pw", "Alternative", "alternative") == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:bridge-proof"));
    ASSERT(switch_to("Bob", 0, "wrong", "Alternative", "alternative") == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:password"));
    b.destroyed = 1;
    ASSERT(fl_user_db_bridge_put(db, &b) == FL_RESULT_OK);
    /* sudo -i to a password-only Root: Root's password; an elevated account is not asked. */
    ASSERT(switch_to("root", 1, "alternative", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "sudo-refused:password"));
    ASSERT(switch_to("root", 1, "root", NULL, NULL) == FL_RESULT_OK);
    ASSERT(!strcmp(fl_session_current_user(), "Root"));
    /* The waiver is the elevation seen before the reservation. If that mark is gone by the
     * recheck, the password-less sudo -i fails closed and the session stays put. */
    fl_session_test_set_switch_pre_lock(drop_root_elevation);
    ASSERT(switch_to("Root", 1, NULL, NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "sudo-refused:changed"));
    ASSERT(!strcmp(fl_session_current_user(), "Root"));
    fl_session_test_set_switch_pre_lock(NULL);
    ASSERT(restore_root_elevation() == FL_RESULT_OK);
    ASSERT(fl_session_is_elevated_account());
    ASSERT(switch_to("Root", 1, NULL, NULL, NULL) == FL_RESULT_OK);
    /* sudo (elevation): the caller's password for a password-only Root, Root's key once it has one. */
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    ASSERT(!fl_session_sudo_needs_root());
    ASSERT(fl_session_sudo_authenticate("flinstone") == FL_RESULT_OK);
    ASSERT(fl_session_has_elevation());
    fl_session_drop_elevation();
    /* Logout during the check moves the login generation. The password was Flinstone's,
     * and that must not become an elevation token for the session that follows. */
    fl_session_test_set_switch_pre_lock(sudo_logout_during);
    ASSERT(fl_session_sudo_authenticate("flinstone") == FL_RESULT_BUSY);
    ASSERT(!strcmp(s_audit, "sudo-refused:superseded"));
    ASSERT(!fl_session_has_elevation());
    fl_session_test_set_switch_pre_lock(NULL);
    ASSERT(!strcmp(fl_session_current_user(), "Flinstone"));
    /* set_auth_mode does not move the login generation. A password-only check must
     * not grant elevation if Root is KQST by the time the lock is taken again. */
    fl_session_test_set_switch_pre_lock(sudo_root_kqst_during);
    ASSERT(fl_session_sudo_authenticate("flinstone") == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "sudo-refused:mode"));
    ASSERT(!fl_session_has_elevation());
    fl_session_test_set_switch_pre_lock(NULL);
    ASSERT(fl_session_set_auth_mode("Root", FL_USER_AUTH_PASSWORD) == FL_RESULT_OK);
    /* The password that passed is checked again. A change in that window is not a grant. */
    fl_session_test_set_switch_pre_lock(sudo_caller_password_during);
    ASSERT(fl_session_sudo_authenticate("flinstone") == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "sudo-refused:password"));
    ASSERT(!fl_session_has_elevation());
    fl_session_test_set_switch_pre_lock(NULL);
    ASSERT(fl_session_set_password("Flinstone", "flinstone") == FL_RESULT_OK);
    ASSERT(fl_session_sudo_authenticate("root") == FL_RESULT_ACCES);
    ASSERT(fl_session_set_auth_mode("Root", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(fl_session_sudo_needs_root());
    ASSERT(fl_session_sudo_authenticate("flinstone") == FL_RESULT_ACCES);
    ASSERT(fl_session_sudo_authenticate("root") == FL_RESULT_ACCES); /* Root has no token here */
    ASSERT(!strcmp(s_audit, "sudo-refused:kqst"));
    ASSERT(switch_to("Root", 1, "root", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(fl_session_set_auth_mode("Root", FL_USER_AUTH_PASSWORD) == FL_RESULT_OK);
    ASSERT(!strcmp(fl_session_current_user(), "Flinstone"));
    fl_session_set_audit_hook(NULL);
    return 0;
}

static int s_stub_grant;

static int stub_access_parent_grant(const char *current, const char *target, int sudo,
                                    const kq_bridge_t *live_bridges, size_t nbridges, int parent_approval,
                                    int *bridge_index_out, int *needs_parent_grant_out) {
    (void)current, (void)target, (void)sudo, (void)live_bridges, (void)nbridges, (void)parent_approval;
    if (bridge_index_out)
        *bridge_index_out = -1;
    if (needs_parent_grant_out)
        *needs_parent_grant_out = s_stub_grant;
    return FL_KQ_ACCESS_PASSWORD;
}

static void patch_no_access(fl_kq_driver_t *d) { d->access_requirement = NULL; }
static void patch_no_prove(fl_kq_driver_t *d) { d->bridge_prove = NULL; }
static void patch_no_verify(fl_kq_driver_t *d) { d->bridge_verify = NULL; }
static void patch_no_connects(fl_kq_driver_t *d) { d->bridge_connects = NULL; }
static void patch_parent_grant(fl_kq_driver_t *d) { d->access_requirement = stub_access_parent_grant; }

static int exec_on_session_db(const char *stmt) {
    sqlite3 *sql = NULL;
    int rc;

    if (sqlite3_open(fl_session_user_db()->path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    rc = sqlite3_exec(sql, stmt, NULL, NULL, NULL);
    sqlite3_close(sql);
    return rc == SQLITE_OK ? 0 : -1;
}

/*
 * FL-12 refusals with no other test: a driver without an op, a parent's grant, more live bridges than
 * the session holds, a store it cannot read, and a KQST-mode account whose token or pairing cannot be
 * read. Each refuses; none falls to the password-only (legacy) branch.
 */
static int test_switch_refusals(void) {
    fl_user_db_t *db = fl_session_user_db_mut();
    fl_user_bridge_t b;
    fl_kq_requirement_t r;
    char uid[40], kqst[300];
    sqlite3 *lock = NULL;
    unsigned i;
    FILE *fp;

    fl_session_set_audit_hook(capture_audit);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    /* No access_requirement op: the requirement is REFUSED and so is the switch. */
    s_patch = patch_no_access;
    r = fl_kq_unlock_requirement("Flinstone", "Bob", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_REFUSED);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:policy"));
    /* A parent's grant: nothing collects one, so refused before any password is checked. */
    s_patch = patch_parent_grant;
    s_stub_grant = 1;
    r = fl_kq_unlock_requirement("Flinstone", "Bob", 0);
    ASSERT(r.factor == FL_KQ_ACCESS_PASSWORD && r.needs_parent_grant == 1);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:parent-grant"));
    s_stub_grant = 0;
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_OK); /* the stub alone lets it through */
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    /* No bridge_connects op: no reach. */
    s_patch = patch_no_connects;
    ASSERT(fl_heritage_bridge_connects("Root.Flinstone.Bob", "Root.Alternative.Ann", "Root.Flinstone.Bob",
                                       "Root.Alternative.Ann") == 0);
    s_patch = NULL;
    ASSERT(fl_heritage_bridge_connects("Root.Flinstone.Bob", "Root.Alternative.Ann", "Root.Flinstone.Bob",
                                       "Root.Alternative.Ann") == 1);

    /* No bridge_prove or bridge_verify op: no proof, so a bridged switch is refused. */
    make_bridge(&b, "abab0000abab0000abab0000abab0000", "Root.Flinstone.Bob", "Root.Alternative");
    ASSERT(fl_user_db_bridge_put(db, &b) == FL_RESULT_OK);
    ASSERT(fl_session_set_user("Alternative") == FL_RESULT_OK);
    ASSERT(fl_kq_unlock_requirement("Alternative", "Bob", 0).factor == FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST);
    s_patch = patch_no_prove;
    ASSERT(fl_kq_prove_bridge_member(b.uid, "Alternative", "alternative", NULL) == FL_KQ_BRIDGE_INVALID);
    ASSERT(switch_to("Bob", 0, "pw", "Alternative", "alternative") == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:bridge-proof"));
    s_patch = patch_no_verify;
    ASSERT(fl_kq_prove_bridge_member(b.uid, "Alternative", "alternative", NULL) == FL_KQ_BRIDGE_INVALID);
    ASSERT(switch_to("Bob", 0, "pw", "Alternative", "alternative") == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:bridge-proof"));
    s_patch = NULL;

    /* A malformed live row: the list is an error, so the switch is refused, not lowered to the
     * cross-branch factor without the bridge (a password-only target's legacy password). */
    ASSERT(exec_on_session_db("UPDATE account_bridges SET public_key=x'00' WHERE uid="
                              "'abab0000abab0000abab0000abab0000';") == 0);
    ASSERT(fl_kq_unlock_requirement("Alternative", "Bob", 0).factor == FL_KQ_ACCESS_REFUSED);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:policy"));
    ASSERT(!strcmp(fl_session_current_user(), "Alternative"));
    b.destroyed = 1;
    ASSERT(fl_user_db_bridge_put(db, &b) == FL_RESULT_OK);
    /* A store that cannot be read (locked by another writer): refused, not "no bridge". */
    ASSERT(sqlite3_open(fl_session_user_db()->path, &lock) == SQLITE_OK);
    ASSERT(sqlite3_exec(lock, "BEGIN EXCLUSIVE;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(fl_kq_unlock_requirement("Alternative", "Bob", 0).factor == FL_KQ_ACCESS_REFUSED);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:policy"));
    ASSERT(sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(lock);

    /* More live bridges than the session holds: never a partial list, so refused. */
    for (i = 0; i <= FL_USER_BRIDGES_MAX; i++) {
        snprintf(uid, sizeof uid, "%08x000000000000000000%06x", 0xf00du, i);
        make_bridge(&b, uid, "Root.Flinstone.Bob", "Root.Third");
        ASSERT(fl_user_db_bridge_put(db, &b) == FL_RESULT_OK);
    }
    ASSERT(fl_kq_unlock_requirement("Alternative", "Bob", 0).factor == FL_KQ_ACCESS_REFUSED);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:policy"));
    ASSERT(exec_on_session_db("UPDATE account_bridges SET destroyed=1;") == 0);
    ASSERT(fl_kq_unlock_requirement("Alternative", "Bob", 0).factor == FL_KQ_ACCESS_KQST_PASSWORD);

    /* A KQST-mode target whose token file is missing, then whose pairing row cannot be read:
     * refused (kqst), never the password-only branch. */
    snprintf(kqst, sizeof kqst, "%s.bob-missing.kqst", fl_session_user_db()->path);
    unlink(kqst);
    {
        char stmt[700];

        snprintf(stmt, sizeof stmt,
                 "INSERT INTO account_keys (label,purpose,holds_share,minted_by,minted_at,generation,"
                 "kqst_path,device_id,signing_public,encryption_public,ledger_seq,event_hash) VALUES"
                 " ('Root.Flinstone.Bob','own',1,'Root',1,1,'%s',zeroblob(16),zeroblob(32),zeroblob(32),0,"
                 "zeroblob(32));",
                 kqst);
        ASSERT(exec_on_session_db(stmt) == 0);
    }
    ASSERT(fl_session_set_auth_mode("Bob", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:kqst"));
    fp = fopen(kqst, "wb");
    ASSERT(fp && fputs("not a slot token", fp) >= 0 && fclose(fp) == 0);
    ASSERT(exec_on_session_db("UPDATE account_keys SET signing_public=x'00' WHERE label='Root.Flinstone.Bob';") == 0);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(!strcmp(s_audit, "su-refused:kqst"));
    ASSERT(!strcmp(fl_session_current_user(), "Alternative"));
    ASSERT(fl_session_set_auth_mode("Bob", FL_USER_AUTH_PASSWORD) == FL_RESULT_OK);
    ASSERT(exec_on_session_db("DELETE FROM account_keys WHERE label='Root.Flinstone.Bob';") == 0);
    unlink(kqst);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    fl_session_set_audit_hook(NULL);
    return 0;
}

/* su -c's restore takes the old identity back only while the login it was asked in is current. */
static int test_restore_user(void) {
    uint64_t gen;

    fl_session_set_audit_hook(capture_audit);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    gen = fl_session_login_generation();
    ASSERT(fl_session_restore_user("Flinstone", 0) == FL_RESULT_INVAL);
    ASSERT(fl_session_set_user("Bob") == FL_RESULT_OK);
    ASSERT(fl_session_restore_user("Flinstone", gen) == FL_RESULT_OK);
    ASSERT(!strcmp(fl_session_current_user(), "Flinstone"));
    ASSERT(fl_session_logout() == FL_RESULT_OK); /* a new login generation */
    ASSERT(fl_session_set_user("Bob") == FL_RESULT_OK);
    s_audit[0] = '\0';
    ASSERT(fl_session_restore_user("Flinstone", gen) == FL_RESULT_BUSY);
    ASSERT(!strcmp(fl_session_current_user(), "Bob")); /* left as it is */
    ASSERT(!strcmp(s_audit, "su-restore-refused:superseded"));
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    fl_session_set_audit_hook(NULL);
    return 0;
}

/* After the password is accepted, a new live bridge must change the factor, and a writer that
 * already holds the database must keep the switch from committing. */
static int s_recheck_put = -1;
static sqlite3 *s_switch_ex;
static int s_switch_ex_rc = -1;

static void switch_insert_live_bridge(void) {
    fl_user_bridge_t b;

    make_bridge(&b, "f00d0000f00d0000f00d0000f00d0000", "Root.Flinstone.Bob", "Root.Alternative");
    s_recheck_put = fl_user_db_bridge_put(fl_session_user_db(), &b);
}

static void switch_hold_exclusive(void) {
    s_switch_ex_rc = -1;
    if (sqlite3_open(fl_session_user_db()->path, &s_switch_ex) != SQLITE_OK) {
        sqlite3_close(s_switch_ex);
        s_switch_ex = NULL;
        return;
    }
    sqlite3_busy_timeout(s_switch_ex, 0);
    s_switch_ex_rc = sqlite3_exec(s_switch_ex, "BEGIN EXCLUSIVE;", NULL, NULL, NULL);
}

static int test_switch_bridge_recheck(void) {
    fl_session_set_audit_hook(capture_audit);
    ASSERT(exec_on_session_db("UPDATE account_bridges SET destroyed = 1;") == 0);
    ASSERT(fl_session_set_user("Alternative") == FL_RESULT_OK);
    fl_session_test_set_switch_pre_lock(switch_insert_live_bridge);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    ASSERT(s_recheck_put == FL_RESULT_OK);
    ASSERT(!strcmp(s_audit, "su-refused:changed"));
    ASSERT(!strcmp(fl_session_current_user(), "Alternative"));
    fl_session_test_set_switch_pre_lock(NULL);
    ASSERT(exec_on_session_db("UPDATE account_bridges SET destroyed = 1;") == 0);

    s_switch_ex = NULL;
    fl_session_test_set_switch_pre_lock(switch_hold_exclusive);
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_BUSY);
    ASSERT(!strcmp(s_audit, "su-refused:busy"));
    ASSERT(!strcmp(fl_session_current_user(), "Alternative"));
    fl_session_test_set_switch_pre_lock(NULL);
    ASSERT(s_switch_ex && s_switch_ex_rc == SQLITE_OK);
    ASSERT(sqlite3_exec(s_switch_ex, "ROLLBACK;", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(s_switch_ex);
    s_switch_ex = NULL;
    ASSERT(switch_to("Bob", 0, "pw", NULL, NULL) == FL_RESULT_OK);
    ASSERT(!strcmp(fl_session_current_user(), "Bob"));
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    fl_session_set_audit_hook(NULL);
    return 0;
}

/* A second thread deletes or changes a password while a switch is in the pre-lock window or in the
 * locked stretch between the recheck and the commit. The mutation goes through the session lock. */
static pthread_t s_mut_thread;
static pthread_barrier_t s_mut_go, s_mut_done;
static atomic_int s_mut_phase;
static int s_mut_rc = -1;
static int s_mut_op; /* 0 delete, 1 set password */
static char s_mut_user[FL_USER_NAME_MAX];
static char s_mut_newpw[32];
static int s_during_phase = -1;
static int s_during_present = -1;
static int s_during_oldpw = -1;

static void *session_mutator(void *arg) {
    (void)arg;
    pthread_barrier_wait(&s_mut_go);
    atomic_store(&s_mut_phase, 1);
    if (s_mut_op)
        s_mut_rc = fl_session_set_password(s_mut_user, s_mut_newpw);
    else
        s_mut_rc = fl_session_remove_user(s_mut_user);
    atomic_store(&s_mut_phase, 2);
    pthread_barrier_wait(&s_mut_done);
    return NULL;
}

static int start_mutator(const char *user, int op, const char *newpw) {
    snprintf(s_mut_user, sizeof s_mut_user, "%s", user);
    snprintf(s_mut_newpw, sizeof s_mut_newpw, "%s", newpw ? newpw : "");
    s_mut_op = op;
    s_mut_rc = -1;
    atomic_store(&s_mut_phase, 0);
    s_during_phase = s_during_present = s_during_oldpw = -1;
    if (pthread_barrier_init(&s_mut_go, NULL, 2) != 0 || pthread_barrier_init(&s_mut_done, NULL, 2) != 0)
        return -1;
    if (pthread_create(&s_mut_thread, NULL, session_mutator, NULL) != 0)
        return -1;
    return 0;
}

static void finish_mutator(int wait_done) {
    if (wait_done)
        pthread_barrier_wait(&s_mut_done);
    pthread_join(s_mut_thread, NULL);
    pthread_barrier_destroy(&s_mut_go);
    pthread_barrier_destroy(&s_mut_done);
    fl_session_test_set_switch_pre_lock(NULL);
    fl_session_test_set_switch_pre_commit(NULL);
}

static void pre_lock_finish_mutator(void) {
    pthread_barrier_wait(&s_mut_go);
    pthread_barrier_wait(&s_mut_done);
}

static void pre_commit_release_mutator(void) {
    const fl_user_db_t *db = fl_session_user_db();

    pthread_barrier_wait(&s_mut_go);
    while (atomic_load(&s_mut_phase) < 1)
        sched_yield();
    s_during_phase = atomic_load(&s_mut_phase);
    s_during_present = fl_user_db_find(db, s_mut_user) != NULL;
    s_during_oldpw = fl_user_db_verify_password(db, s_mut_user, "pw");
}

static int test_switch_mutation_barriers(void) {
    const fl_user_db_t *db;

    fl_session_set_audit_hook(capture_audit);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    ASSERT(fl_session_add_user_under("Root.Flinstone", "Pax", "pw", 3200, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_session_add_user_under("Root.Flinstone", "Qin", "pw", 3201, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_session_add_user_under("Root.Flinstone", "Rex", "pw", 3202, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_session_add_user_under("Root.Flinstone", "Sue", "pw", 3203, 0, 0) == FL_RESULT_OK);

    /* Before the recheck: the mutation finishes, then the reservation sees it and refuses. */
    ASSERT(start_mutator("Pax", 1, "pw2") == 0);
    fl_session_test_set_switch_pre_lock(pre_lock_finish_mutator);
    ASSERT(switch_to("Pax", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    finish_mutator(0);
    ASSERT(s_mut_rc == FL_RESULT_OK);
    ASSERT(!strcmp(s_audit, "su-refused:changed"));
    ASSERT(!strcmp(fl_session_current_user(), "Flinstone"));
    db = fl_session_user_db();
    ASSERT(fl_user_db_verify_password(db, "Pax", "pw2"));
    ASSERT(!fl_user_db_verify_password(db, "Pax", "pw"));

    ASSERT(start_mutator("Qin", 0, NULL) == 0);
    fl_session_test_set_switch_pre_lock(pre_lock_finish_mutator);
    ASSERT(switch_to("Qin", 0, "pw", NULL, NULL) == FL_RESULT_ACCES);
    finish_mutator(0);
    ASSERT(s_mut_rc == FL_RESULT_OK);
    ASSERT(!strcmp(s_audit, "su-refused:changed"));
    ASSERT(!strcmp(fl_session_current_user(), "Flinstone"));
    ASSERT(fl_user_db_find(fl_session_user_db(), "Qin") == NULL);

    /* Between the recheck and the commit: the mutator blocks on the session mutex until the
     * switch releases it, so the rechecked record is still the one that commits. */
    ASSERT(start_mutator("Rex", 1, "pw2") == 0);
    fl_session_test_set_switch_pre_commit(pre_commit_release_mutator);
    ASSERT(switch_to("Rex", 0, "pw", NULL, NULL) == FL_RESULT_OK);
    finish_mutator(1);
    ASSERT(s_mut_rc == FL_RESULT_OK);
    ASSERT(s_during_phase == 1 && s_during_present == 1 && s_during_oldpw == 1);
    ASSERT(!strcmp(fl_session_current_user(), "Rex"));
    db = fl_session_user_db();
    ASSERT(fl_user_db_verify_password(db, "Rex", "pw2"));
    ASSERT(!fl_user_db_verify_password(db, "Rex", "pw"));
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);

    ASSERT(start_mutator("Sue", 0, NULL) == 0);
    fl_session_test_set_switch_pre_commit(pre_commit_release_mutator);
    ASSERT(switch_to("Sue", 0, "pw", NULL, NULL) == FL_RESULT_OK);
    finish_mutator(1);
    /* The switch committed Sue before the removal took the lock, so the removal is refused
     * and Sue stays. Once the session has moved on, the same account can be removed. */
    ASSERT(s_mut_rc == FL_RESULT_ACCES);
    ASSERT(s_during_phase == 1 && s_during_present == 1);
    ASSERT(!strcmp(fl_session_current_user(), "Sue"));
    ASSERT(fl_user_db_find(fl_session_user_db(), "Sue") != NULL);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    ASSERT(fl_session_remove_user("Sue") == FL_RESULT_OK);
    ASSERT(fl_user_db_find(fl_session_user_db(), "Sue") == NULL);

    fl_session_set_audit_hook(NULL);
    return 0;
}

/* useradd's save fails and the following reload fails closed: the command reports it, the
 * in-memory store publishes nothing, and the file on disk is left as it was. A second
 * connection holds the store exclusively, the same way a locked bridge list fails a read,
 * so the save cannot commit. A mode-0444 file is not that lock: a privileged runner can
 * still write it. */
static int test_account_command_reload_fails_closed(void) {
    char path[256];
    char *argv[] = {"useradd", "Zed", NULL};
    int rc;
    int failed = 0;
    sqlite3 *lock = NULL;
    fl_elevation_token_t tok = FL_ELEVATION_TOKEN_NONE;
    uint64_t gen;

    if (fl_session_set_user("Root") != FL_RESULT_OK || !fl_session_is_elevated_account())
        return 1;
    snprintf(path, sizeof path, "%s", fl_session_user_db()->path);
    if (!path[0] || auth_exec_sql(path, "UPDATE users SET auth_mode=2 WHERE name='Root';") != 0)
        return 1;
    if (sqlite3_open(path, &lock) != SQLITE_OK ||
        sqlite3_exec(lock, "BEGIN EXCLUSIVE;", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(lock);
        return 1;
    }
    /* A token and a sudo scope are the two ways has_elevation stays true after the
     * store itself is no longer loaded. The failed reload has to drop both. */
    fl_session_begin_sudo_scope();
    if (fl_elevation_grant("Root", "reload", &tok) != FL_RESULT_OK ||
        setenv("FL_CMD_PASSWORD", "zed-pw", 1) != 0) {
        sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(lock);
        fl_session_end_sudo_scope();
        return 1;
    }
    fl_session_bind_elevation(tok);
    gen = fl_session_login_generation();
    rc = cmd_useradd_run(2, argv);
    unsetenv("FL_CMD_PASSWORD");
    sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(lock);
    if (rc == 0 || fl_session_users_loaded() || fl_session_user_db()->count != 0)
        failed = 1;
    if (fl_session_has_elevation() || fl_session_in_sudo_scope() ||
        fl_session_current_user()[0] != '\0' || fl_session_login_generation() == gen)
        failed = 1;
    if (!auth_cell_is(path, "Root", SQLITE_INTEGER, 2, NULL) || auth_cell_is(path, "Zed", SQLITE_INTEGER, 0, NULL))
        failed = 1;
    if (auth_exec_sql(path, "UPDATE users SET auth_mode=0 WHERE name='Root';") != 0 ||
        fl_session_reload_users() != FL_RESULT_OK || !fl_session_users_loaded() ||
        fl_session_set_user("Flinstone") != FL_RESULT_OK)
        return 1;
    return failed;
}

static int s_switch_denies;

static fl_authz_decision_t deny_identity_switch(unsigned op, void *ctx) {
    (void)ctx;
    if (op == (unsigned)FL_AUTHZ_OP_IDENTITY_SWITCH) {
        s_switch_denies++;
        return FL_AUTHZ_DENY;
    }
    return FL_AUTHZ_ALLOW;
}

/* A deny hook stops su and sudo before either asks for a password, including the
 * sudo <command> path that never enters cmd_sudo_run. */
static int test_identity_switch_denied_before_prompt(void) {
    char *su_argv[] = {"su", "Root", NULL};
    char *sudo_argv[] = {"sudo", "-i", NULL};
    char *sudo_k_argv[] = {"sudo", "-k", NULL};

    ASSERT(fl_authz_subsystem_op_for_shell_cmd((unsigned)FL_SCMD_SU) ==
           (unsigned)FL_AUTHZ_OP_IDENTITY_SWITCH);
    ASSERT(fl_authz_subsystem_op_for_shell_cmd((unsigned)FL_SCMD_SUDO) ==
           (unsigned)FL_AUTHZ_OP_IDENTITY_SWITCH);
    ASSERT(fl_session_set_user("Flinstone") == FL_RESULT_OK);
    unsetenv("FL_CMD_PASSWORD");
    s_switch_denies = 0;
    fl_authz_subsystem_set_hook(deny_identity_switch, NULL);
    ASSERT(cmd_su_run(2, su_argv) != 0);
    ASSERT(cmd_sudo_run(2, sudo_argv) != 0);
    ASSERT(cmd_sudo_require_auth() != 0);
    /* Giving elevation up is not a switch: sudo -k works while switching is denied, and is not counted. */
    ASSERT(cmd_sudo_run(2, sudo_k_argv) == 0);
    fl_authz_subsystem_set_hook(NULL, NULL);
    ASSERT(s_switch_denies == 3);
    ASSERT(!strcmp(fl_session_current_user(), "Flinstone"));
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
    RUN(test_bridge_reach);
    RUN(test_auth_mode_and_kqst_path);
    RUN(test_seed_and_accounts);
    RUN(test_legacy_migration);
    RUN(test_legacy_case_collision);
    RUN(test_bridges);
    RUN(test_legacy_bridge_migration);
    RUN(test_legacy_bridge_mixed_case_columns);
    RUN(test_signed_bridges);
    RUN(test_box_keys);
    RUN(test_share_gate);
    /* authmode RUN block. bridgeproof appends race RUNs at the end of main; leave those alone. */
    RUN(test_auth_mode_reject_malformed);
    RUN(test_auth_mode_startup_fail_closed);
    RUN(test_jail_homes, jail_parent);
    RUN(test_private_bridge_store);
    RUN(test_unlock_requirement);
    RUN(test_session_switch);
    RUN(test_switch_refusals);
    RUN(test_restore_user);
    RUN(test_switch_bridge_recheck);
    RUN(test_switch_mutation_barriers);
    RUN(test_account_command_reload_fails_closed);
    RUN(test_identity_switch_denied_before_prompt);

    if (chdir("/tmp") != 0)
        rc = 1;
    rmrf(jail_parent);
    free(jail_parent);
    unlink(db_path);
    if (rc == 0)
        printf("All heritage tests passed.\n");
    return rc;
}
