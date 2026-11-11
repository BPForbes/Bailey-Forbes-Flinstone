/*
 * test_kq_real_token.c - real slot tokens and a real private bridge (Flinstone #407; KeyQuorum
 * KQ-11 #167, KQ-17 #165, KQ-19 #171).
 *
 * Run by scripts/kq_real_token_fixture.sh (make test_kq_real_token), which mints the material
 * with the keyquorum CLI: two KQSTs on two devices under passphrases drawn at run time, a third
 * device id that holds neither, and a KeyQuorum private bridge between the two slots. This test
 * registers the pairing facts in Flinstone's account store, opens the tokens with kq_slot_open
 * (through the driver), signs with the opened key, logs in, and crosses the bridge at su with a
 * real prove and verify. No secret is a literal: the passphrases are read from the fixture's files.
 */
#include "fl/kq.h"
#include "fl/driver/keyquorum.h"
#include "fl/hex.h"
#include "mem_domain.h"
#include "session.h"
#include "user_db.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static char s_bob_pw[128], s_ann_pw[128];
static char s_audit_last[64];

static const char *env(const char *name) {
    const char *v = getenv(name);

    return v ? v : "";
}

static int read_pass(const char *file, char *out, size_t cap) {
    char path[512];
    FILE *fp;
    size_t n;

    snprintf(path, sizeof path, "%s/%s", env("FL_RT_DIR"), file);
    fp = fopen(path, "r");
    if (!fp || !fgets(out, (int)cap, fp)) {
        if (fp)
            fclose(fp);
        return -1;
    }
    fclose(fp);
    n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = '\0';
    return n ? 0 : -1;
}

static int s_opened_ok;

/* The key service's place at a KQST login: it gets the opened signing secret. Here it checks that the
 * secret is the paired key and signs with it, then accepts the login. */
static fl_result_t opened_hook(const fl_user_record_t *account, const uint8_t secret[32], uint64_t gen) {
    const fl_kq_driver_t *kq = fl_kq_driver_get_or_register();
    unsigned char dev[16], sig_pub[32], enc_pub[32], pub[32], nonce[16], sig[64];

    (void)gen;
    s_opened_ok = 0;
    if (!kq || fl_user_db_kqst_pairing(fl_session_user_db(), account->key_id, dev, sig_pub, enc_pub) != FL_RESULT_OK ||
        kq->account_public(secret, pub) != FL_KQ_ACCOUNT_OK || memcmp(pub, sig_pub, 32) != 0)
        return FL_RESULT_ACCES;
    memset(nonce, 0x5e, sizeof nonce);
    if (kq->account_bridge_sign(FL_KQ_ACCOUNT_BRIDGE_REQUEST, account->key_id, "Root.Other.Peer",
                                account->key_id, nonce, secret, sig) != FL_KQ_ACCOUNT_OK ||
        kq->account_bridge_verify(FL_KQ_ACCOUNT_BRIDGE_REQUEST, account->key_id, "Root.Other.Peer",
                                  account->key_id, nonce, sig_pub, sig) != 1)
        return FL_RESULT_ACCES;
    s_opened_ok = 1;
    return FL_RESULT_OK;
}

static void audit_capture(const char *actor, const char *event, const char *subject, const char *detail) {
    (void)actor;
    (void)subject;
    snprintf(s_audit_last, sizeof s_audit_last, "%s:%s", event ? event : "", detail ? detail : "");
}

/* The pairing the store holds for label: its token's path, device id and the two public keys. */
static int register_pairing(const char *label, const char *kqst, const char *device_hex,
                            const char *sig_hex, const char *enc_hex) {
    unsigned char dev[16], sig[32], enc[32];
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    int ok;

    if (fl_hex_decode(device_hex, dev, sizeof dev) != 0 || fl_hex_decode(sig_hex, sig, sizeof sig) != 0 ||
        fl_hex_decode(enc_hex, enc, sizeof enc) != 0)
        return -1;
    if (sqlite3_open(fl_session_user_db()->path, &sql) != SQLITE_OK)
        return -1;
    ok = sqlite3_prepare_v2(sql,
                            "INSERT OR REPLACE INTO account_keys (label,purpose,holds_share,minted_by,minted_at,"
                            "generation,kqst_path,device_id,signing_public,encryption_public,ledger_seq,event_hash)"
                            " VALUES (?,'own',1,'Root',1,1,?,?,?,?,0,x'00');",
                            -1, &st, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(st, 1, label, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, kqst, -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 3, dev, 16, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 4, sig, 32, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 5, enc, 32, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }
    sqlite3_close(sql);
    return ok ? 0 : -1;
}

static int set_up_accounts(void) {
    fl_user_db_t *db = fl_session_user_db_mut();

    ASSERT(db);
    ASSERT(fl_session_add_user_under("Root.Flinstone", "Bob", s_bob_pw, 3001, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_session_add_user_under("Root.Alternative", "Ann", s_ann_pw, 3002, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_session_set_auth_mode("Bob", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(fl_session_set_auth_mode("Ann", FL_USER_AUTH_PASSWORD_KQST) == FL_RESULT_OK);
    ASSERT(fl_session_save_users() == FL_RESULT_OK);
    ASSERT(register_pairing("Root.Flinstone.Bob", env("FL_RT_BOB_KQST"), env("FL_RT_BOB_DEVICE"),
                            env("FL_RT_BOB_SIG"), env("FL_RT_BOB_ENC")) == 0);
    ASSERT(register_pairing("Root.Alternative.Ann", env("FL_RT_ANN_KQST"), env("FL_RT_ANN_DEVICE"),
                            env("FL_RT_ANN_SIG"), env("FL_RT_ANN_ENC")) == 0);
    return 0;
}

/* A real KQST opens with its passphrase, its key is the one the store paired, and it signs. */
static int test_open_and_sign(void) {
    const fl_kq_driver_t *kq = fl_kq_driver_get_or_register();
    uint8_t secret[32], pub[32], expect[32], nonce[16], sig[64];

    ASSERT(kq);
    ASSERT(fl_kq_open_account_secrets("Bob", s_bob_pw, NULL, secret) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->account_public(secret, pub) == FL_KQ_ACCOUNT_OK);
    ASSERT(fl_hex_decode(env("FL_RT_BOB_SIG"), expect, sizeof expect) == 0);
    ASSERT(memcmp(pub, expect, 32) == 0);
    memset(nonce, 0x3c, sizeof nonce);
    ASSERT(kq->account_bridge_sign(FL_KQ_ACCOUNT_BRIDGE_REQUEST, "Root.Flinstone.Bob", "Root.Alternative.Ann",
                                   "Root.Flinstone.Bob", nonce, secret, sig) == FL_KQ_ACCOUNT_OK);
    kq->wipe(secret, sizeof secret);
    ASSERT(kq->account_bridge_verify(FL_KQ_ACCOUNT_BRIDGE_REQUEST, "Root.Flinstone.Bob",
                                     "Root.Alternative.Ann", "Root.Flinstone.Bob", nonce, expect, sig) == 1);
    sig[5] ^= 1u;
    ASSERT(kq->account_bridge_verify(FL_KQ_ACCOUNT_BRIDGE_REQUEST, "Root.Flinstone.Bob",
                                     "Root.Alternative.Ann", "Root.Flinstone.Bob", nonce, expect, sig) == 0);
    return 0;
}

/* The wrong passphrase and the wrong device are both refused, and so is the login. */
static int test_refusals(void) {
    ASSERT(fl_kq_open_account("Bob", s_ann_pw, NULL) == FL_KQ_ACCOUNT_WRONG_PASSWORD);
    ASSERT(fl_session_login("Bob", s_ann_pw) == FL_RESULT_ACCES);
    /* Bob's token, but the store pairs it with a real device that does not hold it. */
    ASSERT(register_pairing("Root.Flinstone.Bob", env("FL_RT_BOB_KQST"), env("FL_RT_OTHER_DEVICE"),
                            env("FL_RT_BOB_SIG"), env("FL_RT_BOB_ENC")) == 0);
    ASSERT(fl_kq_open_account("Bob", s_bob_pw, NULL) == FL_KQ_ACCOUNT_INVALID);
    ASSERT(fl_session_login("Bob", s_bob_pw) == FL_RESULT_ACCES);
    /* Ann's token, with its own passphrase, presented as Bob's: the label and keys do not match. */
    ASSERT(fl_kq_open_account("Bob", s_ann_pw, env("FL_RT_ANN_KQST")) == FL_KQ_ACCOUNT_INVALID);
    ASSERT(register_pairing("Root.Flinstone.Bob", env("FL_RT_BOB_KQST"), env("FL_RT_BOB_DEVICE"),
                            env("FL_RT_BOB_SIG"), env("FL_RT_BOB_ENC")) == 0);
    ASSERT(fl_kq_open_account("Bob", s_bob_pw, NULL) == FL_KQ_ACCOUNT_OK);
    return 0;
}

static void logout_during_open(void) {
    (void)fl_session_logout();
}

/* The key service's place for the box secret of a KQST login: here it checks the secret is the paired one (its
 * public key is the token's encryption key in the account store) and records that it arrived. */
static int s_box_seen;
static fl_result_t box_hook(const fl_user_record_t *account, const uint8_t box_secret[32], uint64_t gen) {
    const fl_kq_driver_t *kq = fl_kq_driver_get_or_register();
    unsigned char dev[16], sig_pub[32], enc_pub[32], pub[32];

    (void)gen;
    if (!kq || !kq->account_box_public ||
        fl_user_db_kqst_pairing(fl_session_user_db(), account->key_id, dev, sig_pub, enc_pub) != FL_RESULT_OK ||
        kq->account_box_public(box_secret, pub) != FL_KQ_ACCOUNT_OK || memcmp(pub, enc_pub, 32) != 0)
        return FL_RESULT_ACCES; /* not the paired box key: the login would be refused */
    s_box_seen++;
    return FL_RESULT_OK;
}

/* A KQST login commits only while its generation is current (#407: needs a token that opens). */
static int test_login_generation(void) {
    uint64_t before, after;

    ASSERT(fl_session_logout() == FL_RESULT_OK);
    /* A logout while the token is opening supersedes the login: it must not commit. */
    fl_session_test_set_kqst_pre_open(logout_during_open);
    ASSERT(fl_session_login("Bob", s_bob_pw) == FL_RESULT_ACCES);
    fl_session_test_set_kqst_pre_open(NULL);
    ASSERT(strcmp(fl_session_current_user(), "Bob") != 0);
    before = fl_session_login_generation();
    s_opened_ok = 0;
    s_box_seen = 0;
    ASSERT(fl_session_login("Bob", s_bob_pw) == FL_RESULT_OK);
    ASSERT(s_opened_ok == 1); /* the opened key was the paired one and it signed */
    ASSERT(s_box_seen == 1);  /* and the box secret came with it, once, to be kept for the session */
    ASSERT(strcmp(fl_session_current_user(), "Bob") == 0);
    after = fl_session_login_generation();
    ASSERT(after != before && after != 0);
    return 0;
}

static int blob(sqlite3_stmt *st, int col, unsigned char *out, size_t len) {
    if (sqlite3_column_bytes(st, col) != (int)len || !sqlite3_column_blob(st, col))
        return -1;
    memcpy(out, sqlite3_column_blob(st, col), len);
    return 0;
}

/* Imports the KeyQuorum store's bridge (made by bridge private create) with the production importer, then
 * reads it back. tamper_roster: the roster key of Root.Flinstone.Bob then disagrees with the member's own
 * (the same fault the old in-test copy injected before its write). */
static int tamper_roster_signing(const char *uid);

static int import_fixture_bridge(fl_user_bridge_t *b, int tamper_roster) {
    const fl_user_db_t *db = fl_session_user_db();

    memset(b, 0, sizeof *b);
    ASSERT(fl_user_db_bridge_import_from_kq(db, env("FL_RT_ORG_DB"), env("FL_RT_BRIDGE_UID"),
                                            "Root.Flinstone.Bob") == FL_RESULT_OK);
    ASSERT(fl_user_db_bridge_get(db, env("FL_RT_BRIDGE_UID"), b) == FL_RESULT_OK);
    ASSERT(b->nmembers == 2 && !b->destroyed);
    if (tamper_roster)
        ASSERT(tamper_roster_signing(b->uid) == 0);
    return 0;
}

/* su across branches over a live private bridge: the target's password, plus a member opening their
 * token and proving membership against the store's current facts. */
/*
 * Test-only seam: linked with -Wl,--wrap=fl_kq_driver_get_or_register (Makefile, test_kq_real_token).
 * With s_destroy_after_verify set, bridge_verify runs for real and then the bridge is marked destroyed
 * in the store: the roster changing between the proof and the commit.
 */
const fl_kq_driver_t *__real_fl_kq_driver_get_or_register(void);
const fl_kq_driver_t *__wrap_fl_kq_driver_get_or_register(void);
static fl_user_bridge_t *s_destroy_after_verify;
static fl_kq_driver_t s_patched;
static int (*s_real_verify)(const char *, uint32_t, const uint8_t *, const uint8_t *, int, const char *const *,
                            const uint8_t *, size_t, const char *, const uint8_t *, size_t, const uint8_t *,
                            size_t);

/* Marks the bridge destroyed or live in the session's store in place (a re-put would drop the sealed
 * member keys with the roster). */
static int set_destroyed(const char *uid, int destroyed) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    int ok;

    if (sqlite3_open(fl_session_user_db()->path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    ok = sqlite3_prepare_v2(sql, "UPDATE account_bridges SET destroyed = ? WHERE uid = ?;", -1, &st, NULL) ==
             SQLITE_OK &&
         sqlite3_bind_int(st, 1, destroyed) == SQLITE_OK &&
         sqlite3_bind_text(st, 2, uid, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(st) == SQLITE_DONE &&
         sqlite3_changes(sql) == 1;
    sqlite3_finalize(st);
    sqlite3_close(sql);
    return ok ? 0 : -1;
}

enum {
    MUT_NONE = 0,
    MUT_GENERATION,
    MUT_SALT,
    MUT_PUBLIC,
    MUT_SIGNING,
    MUT_ENCRYPTION,
    MUT_SEALED,
    MUT_WRAP,
    MUT_EXCLUSIVE,
    MUT_LOGOUT
};

static int s_mut_kind;
static const char *s_mut_uid;
static sqlite3 *s_block;
static char s_held_path[512];
static int s_held_busy;

static int xor_bound(const char *select_sql, const char *update_sql, int member) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *rd = NULL, *wr = NULL;
    unsigned char buf[FL_USER_BRIDGE_SEALED_MAX];
    const char *label = "Root.Flinstone.Bob";
    int n, ok = 0;

    if (sqlite3_open(fl_session_user_db()->path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    if (sqlite3_prepare_v2(sql, select_sql, -1, &rd, NULL) != SQLITE_OK)
        goto done;
    sqlite3_bind_text(rd, 1, s_mut_uid, -1, SQLITE_TRANSIENT);
    if (member)
        sqlite3_bind_text(rd, 2, label, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(rd) != SQLITE_ROW)
        goto done;
    n = sqlite3_column_bytes(rd, 0);
    if (n <= 0 || (size_t)n > sizeof buf || !sqlite3_column_blob(rd, 0))
        goto done;
    memcpy(buf, sqlite3_column_blob(rd, 0), (size_t)n);
    buf[0] ^= 0x5au;
    if (sqlite3_prepare_v2(sql, update_sql, -1, &wr, NULL) != SQLITE_OK)
        goto done;
    sqlite3_bind_blob(wr, 1, buf, n, SQLITE_TRANSIENT);
    sqlite3_bind_text(wr, 2, s_mut_uid, -1, SQLITE_TRANSIENT);
    if (member)
        sqlite3_bind_text(wr, 3, label, -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(wr) == SQLITE_DONE && sqlite3_changes(sql) == 1;
done:
    sqlite3_finalize(rd);
    sqlite3_finalize(wr);
    sqlite3_close(sql);
    return ok ? 0 : -1;
}

static int bump_generation(void) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    int ok = 0;

    if (sqlite3_open(fl_session_user_db()->path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    if (sqlite3_prepare_v2(sql, "UPDATE account_bridges SET generation = generation + 1 WHERE uid = ?;", -1,
                           &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, s_mut_uid, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(sql) == 1;
        sqlite3_finalize(st);
    }
    sqlite3_close(sql);
    return ok ? 0 : -1;
}

static int tamper_roster_signing(const char *uid) {
    s_mut_uid = uid;
    return xor_bound("SELECT signing_public FROM account_bridge_members WHERE uid = ? AND label = ?;",
                     "UPDATE account_bridge_members SET signing_public = ? WHERE uid = ? AND label = ?;", 1);
}

static int begin_exclusive(void) {
    if (s_block)
        return -1;
    if (sqlite3_open(fl_session_user_db()->path, &s_block) != SQLITE_OK) {
        sqlite3_close(s_block);
        s_block = NULL;
        return -1;
    }
    sqlite3_busy_timeout(s_block, 0);
    if (sqlite3_exec(s_block, "BEGIN EXCLUSIVE;", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(s_block);
        s_block = NULL;
        return -1;
    }
    return 0;
}

/* Same uid and the same member labels; one fact the proof already bound is committed after verify. */
static int apply_mut(void) {
    switch (s_mut_kind) {
    case MUT_GENERATION:
        return bump_generation();
    case MUT_SALT:
        return xor_bound("SELECT salt FROM account_bridges WHERE uid = ?;",
                         "UPDATE account_bridges SET salt = ? WHERE uid = ?;", 0);
    case MUT_PUBLIC:
        return xor_bound("SELECT public_key FROM account_bridges WHERE uid = ?;",
                         "UPDATE account_bridges SET public_key = ? WHERE uid = ?;", 0);
    case MUT_SIGNING:
        return xor_bound("SELECT signing_public FROM account_bridge_members WHERE uid = ? AND label = ?;",
                         "UPDATE account_bridge_members SET signing_public = ? WHERE uid = ? AND label = ?;", 1);
    case MUT_ENCRYPTION:
        return xor_bound(
            "SELECT encryption_public FROM account_bridge_members WHERE uid = ? AND label = ?;",
            "UPDATE account_bridge_members SET encryption_public = ? WHERE uid = ? AND label = ?;", 1);
    case MUT_SEALED:
        return xor_bound(
            "SELECT sealed_bridge_key FROM account_bridge_members WHERE uid = ? AND label = ?;",
            "UPDATE account_bridge_members SET sealed_bridge_key = ? WHERE uid = ? AND label = ?;", 1);
    case MUT_WRAP:
        return xor_bound("SELECT wrap_salt FROM account_bridge_members WHERE uid = ? AND label = ?;",
                         "UPDATE account_bridge_members SET wrap_salt = ? WHERE uid = ? AND label = ?;", 1);
    case MUT_EXCLUSIVE:
        return begin_exclusive();
    case MUT_LOGOUT:
        return fl_session_logout() == FL_RESULT_OK ? 0 : -1;
    default:
        return -1;
    }
}

static int verify_then_act(const char *uid, uint32_t generation, const uint8_t salt[16],
                           const uint8_t bridge_public[32], int destroyed, const char *const *member_labels,
                           const uint8_t *member_signing_pubs, size_t nmembers, const char *verifier_label,
                           const uint8_t *challenge, size_t clen, const uint8_t *artifact, size_t alen) {
    int st = s_real_verify(uid, generation, salt, bridge_public, destroyed, member_labels, member_signing_pubs,
                           nmembers, verifier_label, challenge, clen, artifact, alen);

    if (s_destroy_after_verify && set_destroyed(s_destroy_after_verify->uid, 1) != 0)
        return FL_KQ_BRIDGE_INVALID;
    /* After verify has returned: the proof stands, and the store then changes under it. */
    if (s_mut_kind != MUT_NONE && st == FL_KQ_BRIDGE_OK && apply_mut() != 0)
        return FL_KQ_BRIDGE_INVALID;
    return st;
}

const fl_kq_driver_t *__wrap_fl_kq_driver_get_or_register(void) {
    const fl_kq_driver_t *real = __real_fl_kq_driver_get_or_register();

    if ((!s_destroy_after_verify && s_mut_kind == MUT_NONE) || !real)
        return real;
    s_patched = *real;
    s_real_verify = real->bridge_verify;
    s_patched.bridge_verify = verify_then_act;
    return &s_patched;
}

/* Another connection, while the switch holds the writer reservation. It must be busy, and the row
 * it tried to change must still be the one the proof bound. */
static void held_other_writer(void) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    int stc;

    s_held_busy = 0;
    if (sqlite3_open(s_held_path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return;
    }
    sqlite3_busy_timeout(sql, 0);
    stc = sqlite3_exec(sql, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
    if (stc == SQLITE_BUSY || sqlite3_errcode(sql) == SQLITE_BUSY)
        s_held_busy = 1;
    if (stc == SQLITE_OK)
        (void)sqlite3_exec(sql, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(sql);
    if (sqlite3_open(s_held_path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return;
    }
    sqlite3_busy_timeout(sql, 0);
    if (sqlite3_prepare_v2(sql, "UPDATE account_bridges SET generation = generation + 1;", -1, &st, NULL) ==
        SQLITE_OK) {
        stc = sqlite3_step(st);
        if (stc != SQLITE_BUSY && sqlite3_errcode(sql) != SQLITE_BUSY)
            s_held_busy = 0;
        sqlite3_finalize(st);
    }
    sqlite3_close(sql);
}

static int bridge_generation(const char *uid, int *gen) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    int ok = 0;

    if (sqlite3_open(fl_session_user_db()->path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    if (sqlite3_prepare_v2(sql, "SELECT generation FROM account_bridges WHERE uid = ?;", -1, &st, NULL) ==
        SQLITE_OK) {
        sqlite3_bind_text(st, 1, uid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            *gen = sqlite3_column_int(st, 0);
            ok = 1;
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(sql);
    return ok ? 0 : -1;
}

static int refuse_fact(fl_user_bridge_t *b, int kind) {
    fl_session_switch_request_t req;
    int rc;

    if (import_fixture_bridge(b, 0) != 0 || fl_session_set_user("Bob") != FL_RESULT_OK)
        return -1;
    s_mut_uid = b->uid;
    s_mut_kind = kind;
    memset(&req, 0, sizeof req);
    req.target = "Ann";
    req.password = s_ann_pw;
    req.bridge_member = "Bob";
    req.bridge_member_password = s_bob_pw;
    s_audit_last[0] = '\0';
    rc = fl_session_switch_user(&req);
    s_mut_kind = MUT_NONE;
    if (kind == MUT_LOGOUT) {
        if (rc != FL_RESULT_BUSY || strcmp(s_audit_last, "su-refused:superseded") != 0 ||
            strcmp(fl_session_current_user(), "Ann") == 0)
            return -1;
        return 0;
    }
    if (kind == MUT_EXCLUSIVE) {
        int stayed = strcmp(fl_session_current_user(), "Bob") == 0;

        if (s_block) {
            (void)sqlite3_exec(s_block, "ROLLBACK;", NULL, NULL, NULL);
            sqlite3_close(s_block);
            s_block = NULL;
        }
        if (!stayed || rc != FL_RESULT_BUSY || strcmp(s_audit_last, "su-refused:busy") != 0)
            return -1;
        return 0;
    }
    if (rc != FL_RESULT_ACCES || strcmp(s_audit_last, "su-refused:bridge-changed") != 0 ||
        strcmp(fl_session_current_user(), "Bob") != 0)
        return -1;
    return 0;
}

static int test_bridge_fact_races(void) {
    fl_user_bridge_t *b = calloc(1, sizeof *b);
    fl_session_switch_request_t req;
    static const int kinds[] = {MUT_GENERATION, MUT_SALT,         MUT_PUBLIC, MUT_SIGNING,  MUT_ENCRYPTION,
                                MUT_SEALED,     MUT_WRAP,         MUT_EXCLUSIVE, MUT_LOGOUT};
    size_t i;
    int gen = 0, gen_after = 0;
    int rc = 1;

    if (!b)
        return 1;
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        if (refuse_fact(b, kinds[i]) != 0) {
            fprintf(stderr, "fact kind %d failed (audit %s, user %s)\n", kinds[i], s_audit_last,
                    fl_session_current_user());
            goto out;
        }
    }
    s_mut_kind = MUT_NONE;
    s_destroy_after_verify = NULL;
    if (import_fixture_bridge(b, 0) != 0 || fl_session_set_user("Bob") != FL_RESULT_OK)
        goto out;
    memset(&req, 0, sizeof req);
    req.target = "Ann";
    req.password = s_ann_pw;
    req.bridge_member = "Bob";
    req.bridge_member_password = s_bob_pw;
    if (fl_session_switch_user(&req) != FL_RESULT_OK || strcmp(fl_session_current_user(), "Ann") != 0)
        goto out;
    if (import_fixture_bridge(b, 0) != 0 || fl_session_set_user("Bob") != FL_RESULT_OK)
        goto out;
    if (bridge_generation(b->uid, &gen) != 0)
        goto out;
    snprintf(s_held_path, sizeof s_held_path, "%s", fl_session_user_db()->path);
    s_held_busy = 0;
    fl_session_test_set_switch_pre_commit(held_other_writer);
    if (fl_session_switch_user(&req) != FL_RESULT_OK || strcmp(fl_session_current_user(), "Ann") != 0) {
        fl_session_test_set_switch_pre_commit(NULL);
        goto out;
    }
    fl_session_test_set_switch_pre_commit(NULL);
    if (!s_held_busy || bridge_generation(b->uid, &gen_after) != 0 || gen_after != gen) {
        fprintf(stderr, "held writer busy=%d generation %d -> %d\n", s_held_busy, gen, gen_after);
        goto out;
    }
    rc = 0;
out:
    s_mut_kind = MUT_NONE;
    fl_session_test_set_switch_pre_commit(NULL);
    if (s_block) {
        (void)sqlite3_exec(s_block, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(s_block);
        s_block = NULL;
    }
    if (rc != 0)
        fprintf(stderr, "FAIL [%s]: bridge fact races (last audit %s)\n", __func__, s_audit_last);
    free(b);
    return rc;
}

/* KQ-19 import: the production importer against a store the real CLI made. */
static int test_bridge_import(void) {
    fl_user_bridge_t *b = calloc(1, sizeof *b);
    const fl_user_db_t *db = fl_session_user_db();
    unsigned char sealed[FL_USER_BRIDGE_SEALED_MAX], wrap[16];
    size_t n = 0;
    const char *org = env("FL_RT_ORG_DB"), *uid = env("FL_RT_BRIDGE_UID");
    int rc = 1;

    ASSERT(b);
    /* An unknown uid, and an account that is not on the roster: refused, and nothing is written. */
    if (fl_user_db_bridge_import_from_kq(db, org, "00000000", "Root.Flinstone.Bob") != FL_RESULT_NOENT)
        goto out;
    if (fl_user_db_bridge_import_from_kq(db, org, uid, "Root.Nobody.Else") != FL_RESULT_NOENT)
        goto out;
    if (fl_user_db_bridge_get(db, uid, b) == FL_RESULT_OK && b->nmembers != 0) /* earlier tests may have imported it */
        goto out;
    /* Ann is on the roster but KeyQuorum holds no sealed key of hers: refused whole, no roster left behind. */
    if (fl_user_db_bridge_import_from_kq(db, org, uid, "Root.Alternative.Ann") != FL_RESULT_NOENT)
        goto out;
    if (fl_user_db_bridge_get(db, uid, b) == FL_RESULT_OK && b->nmembers != 0)
        goto out;
    /* A missing store is an error, not an empty bridge. */
    if (fl_user_db_bridge_import_from_kq(db, "/nonexistent/kq.sqlite", uid, "Root.Flinstone.Bob") == FL_RESULT_OK)
        goto out;
    /* Bob's import brings the roster and his sealed key together. */
    if (fl_user_db_bridge_import_from_kq(db, org, uid, "Root.Flinstone.Bob") != FL_RESULT_OK)
        goto out;
    memset(b, 0, sizeof *b);
    if (fl_user_db_bridge_get(db, uid, b) != FL_RESULT_OK || b->nmembers != 2 || b->destroyed || b->generation == 0)
        goto out;
    if (fl_user_db_bridge_sealed_key(db, uid, "Root.Flinstone.Bob", sealed, sizeof sealed, &n, wrap) != FL_RESULT_OK ||
        n == 0)
        goto out;
    /* The store holds none of Ann's: the importer copied only the local member's. */
    if (fl_user_db_bridge_sealed_key(db, uid, "Root.Alternative.Ann", sealed, sizeof sealed, &n, wrap) == FL_RESULT_OK)
        goto out;
    /* Importing again is idempotent. */
    if (fl_user_db_bridge_import_from_kq(db, org, uid, "Root.Flinstone.Bob") != FL_RESULT_OK)
        goto out;
    /* A roster-only import (no local member) is accepted and carries no sealed key. */
    if (fl_user_db_bridge_import_from_kq(db, org, uid, NULL) != FL_RESULT_OK)
        goto out;
    if (fl_user_db_bridge_sealed_key(db, uid, "Root.Flinstone.Bob", sealed, sizeof sealed, &n, wrap) == FL_RESULT_OK)
        goto out; /* re-put replaced the roster and, by the writer's contract, its sealed keys */
    rc = 0;
out:
    mem_domain_secure_zero(sealed, sizeof sealed);
    free(b);
    return rc;
}

static int test_bridge_su(void) {
    fl_user_bridge_t *b = calloc(1, sizeof *b);
    fl_kq_requirement_t need;
    fl_session_switch_request_t req;
    int rc = 1;

    ASSERT(b);
    /* A roster key that disagrees with the member's own: the proof does not verify. */
    if (import_fixture_bridge(b, 1) != 0)
        goto out;
    if (fl_kq_prove_bridge_member(b->uid, "Bob", s_bob_pw, NULL) != FL_KQ_BRIDGE_REFUSED)
        goto fail;
    if (import_fixture_bridge(b, 0) != 0)
        goto out;
    if (fl_kq_prove_bridge_member(b->uid, "Bob", s_bob_pw, NULL) != FL_KQ_BRIDGE_OK)
        goto fail;
    if (fl_kq_prove_bridge_member(b->uid, "Bob", s_ann_pw, NULL) != FL_KQ_BRIDGE_REFUSED)
        goto fail; /* the member's token does not open with another passphrase */
    if (fl_kq_prove_bridge_member(b->uid, "Ann", s_ann_pw, NULL) != FL_KQ_BRIDGE_REFUSED)
        goto fail; /* Ann is a member, but this store holds no sealed key of hers */
    need = fl_kq_unlock_requirement("Bob", "Ann", 0);
    if (need.factor != FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST || strcmp(need.bridge_uid, b->uid) != 0)
        goto fail;

    memset(&req, 0, sizeof req);
    req.target = "Ann";
    req.password = s_ann_pw;
    req.bridge_member = "Bob";
    req.bridge_member_password = s_ann_pw; /* wrong: refused and audited */
    if (fl_session_switch_user(&req) != FL_RESULT_ACCES || strcmp(s_audit_last, "su-refused:bridge-proof") != 0)
        goto fail;
    req.bridge_member = NULL;
    if (fl_session_switch_user(&req) != FL_RESULT_ACCES ||
        strcmp(s_audit_last, "su-refused:bridge-member") != 0)
        goto fail;
    req.bridge_member = "Bob";
    req.bridge_member_password = s_bob_pw;
    /* The proof verifies, but the bridge is gone by the time the session asks again: refused. */
    s_destroy_after_verify = b;
    rc = fl_session_switch_user(&req) != FL_RESULT_ACCES || strcmp(s_audit_last, "su-refused:bridge-changed") != 0 ||
         strcmp(fl_session_current_user(), "Bob") != 0;
    s_destroy_after_verify = NULL;
    if (rc != 0 || set_destroyed(b->uid, 0) != 0)
        goto fail;
    rc = 1;
    if (fl_session_switch_user(&req) != FL_RESULT_OK || strcmp(fl_session_current_user(), "Ann") != 0)
        goto fail;
    /* Destroyed: no longer live, so Ann -> Bob across branches asks for Bob's own token. */
    b->destroyed = 1;
    if (fl_user_db_bridge_put(fl_session_user_db(), b) != FL_RESULT_OK)
        goto fail;
    if (fl_kq_prove_bridge_member(b->uid, "Bob", s_bob_pw, NULL) != FL_KQ_BRIDGE_STALE)
        goto fail;
    need = fl_kq_unlock_requirement("Ann", "Bob", 0);
    if (need.factor != FL_KQ_ACCESS_KQST_PASSWORD)
        goto fail;
    memset(&req, 0, sizeof req);
    req.target = "Bob";
    req.password = s_ann_pw;
    if (fl_session_switch_user(&req) != FL_RESULT_ACCES || strcmp(s_audit_last, "su-refused:password") != 0)
        goto fail;
    req.password = s_bob_pw;
    /* The right password, but the token is not on the paired device: a password alone never does. */
    if (register_pairing("Root.Flinstone.Bob", env("FL_RT_BOB_KQST"), env("FL_RT_OTHER_DEVICE"),
                         env("FL_RT_BOB_SIG"), env("FL_RT_BOB_ENC")) != 0 ||
        fl_session_switch_user(&req) != FL_RESULT_ACCES || strcmp(s_audit_last, "su-refused:kqst") != 0 ||
        register_pairing("Root.Flinstone.Bob", env("FL_RT_BOB_KQST"), env("FL_RT_BOB_DEVICE"),
                         env("FL_RT_BOB_SIG"), env("FL_RT_BOB_ENC")) != 0)
        goto fail;
    if (fl_session_switch_user(&req) != FL_RESULT_OK || strcmp(fl_session_current_user(), "Bob") != 0)
        goto fail;
    rc = 0;
    goto out;
fail:
    fprintf(stderr, "FAIL [%s]: bridge su (last audit %s)\n", __func__, s_audit_last);
out:
    free(b);
    return rc;
}

/* KQ-16 with the real material: a parent's signing seed is dealt to the box keys of two real slot tokens (Bob and
 * Ann, on two real devices), assembled back through the driver from the secrets those tokens open, and the
 * assembled key signs a revision the parent's own key would: the history verifies and the revision is trusted under the parent's
 * registered public key, and not under another's. One device twice is no
 * quorum, and a record checked against another parent's public key is refused. */
static int test_parent_key_assembly(void) {
    const fl_kq_driver_t *kq = fl_kq_driver_get_or_register();
    const char *labels[2] = {"Root.Flinstone.Bob", "Root.Alternative.Ann"};
    uint8_t seed[32], parent_pub[32], other_seed[32], other_pub[32], assembled[32];
    uint8_t bob_sign[32], ann_sign[32], bob_box[32], ann_box[32];
    uint8_t box_pub[2][32], box_sec[2][32], dev[2][16], same_dev[2][16];
    uint8_t record[4096], id[16] = {0x71}, rev[32], rev2[32];
    size_t rlen = 0;
    unsigned gen = 0;
    fl_kq_buf_t base = {0}, own = {0}, viaasm = {0};
    int ok = 0;

    ASSERT(kq && kq->assembly_split && kq->assembly_assemble && kq->assembly_generation);
    ASSERT(kq->account_keygen(seed, parent_pub) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->account_keygen(other_seed, other_pub) == FL_KQ_ACCOUNT_OK);
    ASSERT(fl_kq_open_account_keys("Bob", s_bob_pw, NULL, bob_sign, bob_box) == FL_KQ_ACCOUNT_OK);
    ASSERT(fl_kq_open_account_keys("Ann", s_ann_pw, NULL, ann_sign, ann_box) == FL_KQ_ACCOUNT_OK);
    memcpy(box_sec[0], bob_box, 32);
    memcpy(box_sec[1], ann_box, 32);
    ASSERT(fl_hex_decode(env("FL_RT_BOB_ENC"), box_pub[0], 32) == 0 && fl_hex_decode(env("FL_RT_ANN_ENC"), box_pub[1], 32) == 0);
    ASSERT(fl_hex_decode(env("FL_RT_BOB_DEVICE"), dev[0], 16) == 0 && fl_hex_decode(env("FL_RT_ANN_DEVICE"), dev[1], 16) == 0);
    memcpy(same_dev[0], dev[0], 16);
    memcpy(same_dev[1], dev[0], 16);

    ASSERT(kq->assembly_threshold(2) == 2);
    ASSERT(kq->assembly_split(seed, 7, labels, (const uint8_t(*)[32])box_pub, 2, record, sizeof record, &rlen) ==
           KQ_ASSEMBLY_OK && rlen > 0);
    ASSERT(kq->assembly_generation(record, rlen, &gen) == KQ_ASSEMBLY_OK && gen == 7);
    /* Both real tokens' secrets, two devices: the seed comes back. */
    ASSERT(kq->assembly_assemble(record, rlen, parent_pub, labels, (const uint8_t(*)[32])box_sec,
                                 (const uint8_t(*)[16])dev, 2, 2, assembled) == KQ_ASSEMBLY_OK);
    ASSERT(memcmp(assembled, seed, 32) == 0);
    /* One share is not enough; one device twice does not meet two devices; another parent's key is a mismatch. */
    memset(assembled, 0xAA, sizeof assembled);
    ASSERT(kq->assembly_assemble(record, rlen, parent_pub, labels, (const uint8_t(*)[32])box_sec,
                                 (const uint8_t(*)[16])dev, 1, 2, assembled) == KQ_ASSEMBLY_QUORUM);
    ASSERT(kq->assembly_assemble(record, rlen, parent_pub, labels, (const uint8_t(*)[32])box_sec,
                                 (const uint8_t(*)[16])same_dev, 2, 2, assembled) == KQ_ASSEMBLY_QUORUM);
    ASSERT(kq->assembly_assemble(record, rlen, other_pub, labels, (const uint8_t(*)[32])box_sec,
                                 (const uint8_t(*)[16])dev, 2, 2, assembled) == KQ_ASSEMBLY_MISMATCH);
    ASSERT(assembled[0] == 0xAA); /* nothing written unless OK */
    ASSERT(kq->assembly_assemble(record, rlen, parent_pub, labels, (const uint8_t(*)[32])box_sec,
                                 (const uint8_t(*)[16])dev, 2, 2, assembled) == KQ_ASSEMBLY_OK);

    /* The assembled key signs the same revision the parent's own key does, and the history verifies. */
    ASSERT(kq->file_new(id, "asm.txt", "Root", &base) == KQ_FILE_OK);
    ASSERT(kq->check_in(&base, "Root", "2026-11-10T09:00:00.000Z", (const uint8_t *)"v1\n", 3, seed, &own, rev) == KQ_FILE_OK);
    ASSERT(kq->check_in(&base, "Root", "2026-11-10T09:00:00.000Z", (const uint8_t *)"v1\n", 3, assembled, &viaasm, rev2) ==
           KQ_FILE_OK);
    {
        /* Trusted under the parent's registered public key alone: the signature is the parent's, whichever way the
         * key was obtained. (The two containers need not be byte-identical: a container carries its own nonces.) */
        kq_file_key_t pk = {.label = "Root"};
        kq_file_trust_t trust = {.keys = &pk, .nkeys = 1};
        uint8_t got_own[32], got_asm[32];

        memcpy(pk.signing_public, parent_pub, 32);
        ok = kq->verify(&viaasm, NULL) == KQ_FILE_OK && kq->select_read(&own, &trust, (const uint8_t(*)[32])rev, 1, got_own) == KQ_FILE_OK &&
             kq->select_read(&viaasm, &trust, (const uint8_t(*)[32])rev2, 1, got_asm) == KQ_FILE_OK && memcmp(got_asm, rev2, 32) == 0 &&
             memcmp(got_own, rev, 32) == 0;
        /* and not under another parent's key */
        memcpy(pk.signing_public, other_pub, 32);
        ok = ok && kq->select_read(&viaasm, &trust, (const uint8_t(*)[32])rev2, 1, got_asm) != KQ_FILE_OK;
    }
    fl_kq_buf_free(&base);
    fl_kq_buf_free(&own);
    fl_kq_buf_free(&viaasm);
    kq->wipe(seed, 32);
    kq->wipe(other_seed, 32);
    kq->wipe(assembled, 32);
    kq->wipe(bob_sign, 32);
    kq->wipe(ann_sign, 32);
    kq->wipe(bob_box, 32);
    kq->wipe(ann_box, 32);
    kq->wipe(box_sec, sizeof box_sec);
    ASSERT(ok);
    return 0;
}

int main(void) {
    char users[512];
    int rc = 0;

    if (!env("FL_RT_DIR")[0]) {
        fprintf(stderr, "test_kq_real_token: run through scripts/kq_real_token_fixture.sh\n");
        return 1;
    }
    if (read_pass("bob.pass", s_bob_pw, sizeof s_bob_pw) != 0 ||
        read_pass("ann.pass", s_ann_pw, sizeof s_ann_pw) != 0) {
        fprintf(stderr, "test_kq_real_token: fixture passphrases missing\n");
        return 1;
    }
    snprintf(users, sizeof users, "%s/users.sqlite", env("FL_RT_DIR"));
    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
    (void)setenv("FL_USERS_DB_PATH", users, 1);
    fl_session_init();
    fl_session_set_audit_hook(audit_capture);
    fl_session_set_login_hooks(NULL, NULL, opened_hook);
    fl_session_set_login_box_hook(box_hook);

#define RUN(t) do { if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } else printf("%s... OK\n", #t); } while (0)
    RUN(set_up_accounts);
    RUN(test_open_and_sign);
    RUN(test_refusals);
    RUN(test_login_generation);
    RUN(test_bridge_import);
    RUN(test_bridge_su);
    RUN(test_bridge_fact_races);
    RUN(test_parent_key_assembly);
    mem_domain_secure_zero(s_bob_pw, sizeof s_bob_pw);
    mem_domain_secure_zero(s_ann_pw, sizeof s_ann_pw);
    if (rc == 0)
        printf("All real-token tests passed.\n");
    return rc;
}
