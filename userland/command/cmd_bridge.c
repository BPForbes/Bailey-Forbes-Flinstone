#include "cmd_decl.h"
#include "cmd_batch.h"
#include "fl/audit_log.h"
#include "fl/driver/keyquorum.h"
#include "fl/session.h"
#include "bridge.h"
#include "cmd_authutil.h"
#include "heritage.h"
#include "mem_domain.h"
#include "timekeeping.h"
#include "user_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bridge_usage(void) {
    fprintf(stderr,
            "bridge: usage: bridge list | request <account> | accept <id> | revoke <id>\n"
            "                    | kq-import <keyquorum-store> <uid> [--roster-only]   (with sudo)\n"
            "                    | assembly <parent> [--reissue]                       (with sudo)\n"
            "  Accounts on different branches of the heritage tree reach each other's\n"
            "  homes only through an active bridge: one side requests, the other side\n"
            "  (or an account above it) accepts. Either side, or an account above\n"
            "  either, revokes. Requesting and accepting ask for your password: each\n"
            "  half is signed with your account's KeyQuorum key.\n");
}

typedef struct {
    const fl_user_db_t *db;
    unsigned            shown;
} bridge_list_ctx_t;

static void bridge_print(const fl_bridge_t *b, void *ctx) {
    bridge_list_ctx_t *list = ctx;
    const char *state = "pending";

    /* An active row whose signatures do not verify opens nothing: say so. */
    if (b->state == FL_BRIDGE_STATE_ACTIVE)
        state = fl_bridge_verified(list->db, b) ? "active" : "unsigned";
    printf("#%lld  %-8s  %s <-> %s", (long long)b->id, state, b->a_key, b->b_key);
    if (b->state == FL_BRIDGE_STATE_ACTIVE && b->accepted_by[0] && strcmp(b->accepted_by, b->b_key))
        printf("  (accepted by %s)", b->accepted_by);
    fputc('\n', stdout);
    list->shown++;
}

/* Opens the logged-in account's signing key (cmd_open_account_keys) with bridge's messages. */
static int bridge_open_actor_key(unsigned char *secret) {
    fl_result_t rc = cmd_open_account_keys("signs the bridge", secret, NULL);

    if (rc == FL_RESULT_OK)
        return 0;
    if (rc == FL_RESULT_EOF)
        fprintf(stderr, "bridge: password read failed\n");
    else if (rc == FL_RESULT_ACCES)
        fprintf(stderr, "bridge: incorrect password\n");
    else
        fprintf(stderr, "bridge: cannot open the signing key of %s (%d)\n",
                fl_session_current_user(), (int)rc);
    return -1;
}

static int bridge_parse_id(const char *s, int64_t *out) {
    char *end = NULL;
    long long v;

    if (!s || !s[0])
        return -1;
    v = strtoll(s[0] == '#' ? s + 1 : s, &end, 10);
    if (!end || *end != '\0' || v <= 0)
        return -1;
    *out = (int64_t)v;
    return 0;
}

/* bridge assembly <parent>: deal the parent account's signing seed among its direct child accounts so that
 * enough of them online together can sign for it (KeyQuorum parent-key assembly). The split, the threshold and
 * every seal are KeyQuorum's (the driver's assembly ops); this gathers the facts it needs (the children and
 * their box public keys) and stores the opaque record. The parent's own password opens the seed once, here. */
static int bridge_assembly(fl_user_db_t *db, const char *actor, const char *parent_name, int reissue) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    const fl_user_record_t *parent = fl_user_db_find(db, parent_name);
    const char *labels[FL_USER_DB_MAX_USERS];
    unsigned char boxes[FL_USER_DB_MAX_USERS][32];
    unsigned char seed[FL_USER_SIGNING_SECRET_LEN];
    unsigned char *rec;
    char pw[256], up[FL_HERITAGE_KEY_ID_MAX], parent_key[FL_HERITAGE_KEY_ID_MAX];
    size_t n = 0, rlen = 0;
    unsigned threshold, gen = 0, old_gen = 0, old_thr = 0;
    fl_result_t rc;
    int st, ret = 1;

    if (!fl_session_has_elevation()) {
        fprintf(stderr, "bridge: assembly changes who can sign for %s; run it with sudo\n", parent_name);
        return 1;
    }
    if (!parent) {
        fprintf(stderr, "bridge: %s does not exist\n", parent_name);
        return 1;
    }
    mem_domain_copy(parent_key, parent->key_id, sizeof parent_key);
    if (fl_heritage_relation(actor, parent_key) != FL_HERITAGE_REL_SELF &&
        fl_heritage_relation(actor, parent_key) != FL_HERITAGE_REL_DESCENDANT) {
        fprintf(stderr, "bridge: %s is not this account or below it\n", parent_name);
        return 1;
    }
    if (reissue) {
        /* A reissue rotates the parent's signing key first, then deals the new seed at generation + 1 (re-dealing
         * the old seed would leave a removed child's share valid). The account store has no function that rotates
         * a signing key on its own: the only existing rotation (passwd by an ancestor) also replaces the password.
         * Inventing a second rotation path here would split the key rules, so this stops. */
        fprintf(stderr, "bridge: assembly --reissue is not available: there is no signing-key rotation that keeps the "
                        "password (see docs/IDENTITY_HERITAGE.md)\n");
        return 1;
    }
    if (!drv || !drv->assembly_split || !drv->assembly_threshold) {
        fprintf(stderr, "bridge: assembly needs the KeyQuorum driver (not in this build)\n");
        return 1;
    }
    if (!parent->has_key) {
        fprintf(stderr, "bridge: %s has no signing key yet; it must sign in once first\n", parent_name);
        return 1;
    }
    for (size_t i = 0; i < db->count && n < FL_USER_DB_MAX_USERS; i++) {
        const fl_user_record_t *c = &db->users[i];

        if (!c->has_box || fl_heritage_parent(c->key_id, up, sizeof up) != FL_RESULT_OK || strcmp(up, parent_key) != 0)
            continue;
        labels[n] = c->key_id;
        mem_domain_copy(boxes[n], c->box_public, 32);
        n++;
    }
    if (n == 0) {
        fprintf(stderr, "bridge: %s has no child account with a box key (each child must sign in once)\n", parent_name);
        return 1;
    }
    rec = (unsigned char *)mem_domain_alloc(MEM_DOMAIN_FS, FL_USER_ASSEMBLY_MAX);
    if (!rec)
        return 1;
    if (fl_user_db_assembly_get(db, parent_key, rec, FL_USER_ASSEMBLY_MAX, &rlen, &old_gen, &old_thr) == FL_RESULT_OK) {
        fprintf(stderr, "bridge: %s already has an assembly (generation %u); use --reissue after removing a child\n",
                parent_name, old_gen);
        goto out;
    }
    if (cmd_read_password("Password of the parent: ", pw, sizeof pw) != 0) {
        fprintf(stderr, "bridge: password read failed\n");
        goto out;
    }
    rc = fl_user_db_open_key(db, parent_name, pw, seed);
    cmd_wipe_password(pw, sizeof pw);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "bridge: incorrect password for %s\n", parent_name);
        goto out;
    }
    gen = 1;
    threshold = drv->assembly_threshold((unsigned)n);
    st = drv->assembly_split(seed, gen, labels, (const uint8_t(*)[32])boxes, n, rec, FL_USER_ASSEMBLY_MAX, &rlen);
    mem_domain_secure_zero(seed, sizeof seed);
    if (st != KQ_ASSEMBLY_OK || fl_user_db_assembly_put(db, parent_key, gen, threshold, rec, rlen) != FL_RESULT_OK) {
        fprintf(stderr, "bridge: assembly failed (%d)\n", st);
        goto out;
    }
    fl_audit_identity_event(actor, "bridge-assembly", parent_key, "created");
    printf("bridge: %s can now sign when %u of its %zu children are online on separate devices\n", parent_name, threshold, n);
    ret = 0;
out:
    mem_domain_secure_zero(seed, sizeof seed);
    mem_domain_free(MEM_DOMAIN_FS, rec);
    return ret;
}

int cmd_bridge_run(int argc, char **argv) {
    fl_user_db_t *db;
    char actor[FL_HERITAGE_KEY_ID_MAX];
    unsigned char secret[FL_USER_SIGNING_SECRET_LEN];
    int64_t now = 0;
    int64_t id = 0;
    fl_result_t rc;

    fl_session_init();
    db = fl_session_user_db_mut();
    strncpy(actor, fl_session_current_key_id(), sizeof(actor) - 1);
    actor[sizeof(actor) - 1] = '\0';
    if (!fl_heritage_key_valid(actor)) {
        fprintf(stderr, "bridge: the logged-in account has no heritage key id\n");
        return 1;
    }
    if (fl_time_wall_sec(&now) != FL_RESULT_OK)
        now = 0;

    if (argc < 2 || !strcmp(argv[1], "list")) {
        bridge_list_ctx_t list = {db, 0};

        if (fl_bridge_list_visible(db->path, actor, bridge_print, &list) != FL_RESULT_OK) {
            fprintf(stderr, "bridge: cannot read bridges from %s\n", db->path);
            return 1;
        }
        if (!list.shown)
            printf("bridge: no bridges at or below %s\n", actor);
        return 0;
    }

    if (!strcmp(argv[1], "request") && argc == 3) {
        const fl_user_record_t *peer = fl_user_db_find(db, argv[2]);
        char peer_key[FL_HERITAGE_KEY_ID_MAX];
        char detail[32];
        fl_heritage_relation_t rel;

        if (!peer) {
            fprintf(stderr, "bridge: %s does not exist\n", argv[2]);
            return 1;
        }
        strncpy(peer_key, peer->key_id, sizeof(peer_key) - 1);
        peer_key[sizeof(peer_key) - 1] = '\0';
        rel = fl_heritage_relation(actor, peer_key);
        if (rel == FL_HERITAGE_REL_SELF || rel == FL_HERITAGE_REL_ANCESTOR ||
            rel == FL_HERITAGE_REL_DESCENDANT) {
            fprintf(stderr, "bridge: %s is on your own line (%s); no bridge is needed\n", peer_key,
                    fl_heritage_relation_name(rel));
            return 1;
        }
        if (bridge_open_actor_key(secret) != 0)
            return 1;
        rc = fl_bridge_request(db->path, db, actor, peer_key, now, secret, &id);
        mem_domain_zero(secret, sizeof secret);
        if (rc == FL_RESULT_BUSY) {
            fprintf(stderr, "bridge: %s and %s already have a bridge (see `bridge list`)\n", actor,
                    peer_key);
            return 1;
        }
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "bridge: request failed (%d)\n", (int)rc);
            return 1;
        }
        snprintf(detail, sizeof detail, "bridge=%lld", (long long)id);
        fl_audit_identity_event(actor, "bridge-request", peer_key, detail);
        printf("bridge: #%lld requested %s <-> %s (pending until %s or an account above it accepts)\n",
               (long long)id, actor, peer_key, peer_key);
        return 0;
    }

    if ((!strcmp(argv[1], "accept") || !strcmp(argv[1], "revoke")) && argc == 3) {
        int accept = !strcmp(argv[1], "accept");
        fl_bridge_t b;
        char detail[32];

        if (bridge_parse_id(argv[2], &id) != 0) {
            fprintf(stderr, "bridge: %s is not a bridge id\n", argv[2]);
            return 1;
        }
        if (fl_bridge_get(db->path, id, &b) != FL_RESULT_OK) {
            fprintf(stderr, "bridge: no bridge #%lld\n", (long long)id);
            return 1;
        }
        if (accept) {
            if (bridge_open_actor_key(secret) != 0)
                return 1;
            rc = fl_bridge_accept(db->path, db, actor, id, now, secret);
            mem_domain_zero(secret, sizeof secret);
        } else {
            rc = fl_bridge_revoke(db->path, actor, id);
        }
        if (rc == FL_RESULT_INVAL && accept) {
            fprintf(stderr,
                    "bridge: #%lld has no valid signature from %s; revoke it and request again\n",
                    (long long)id, b.a_key);
            return 1;
        }
        if (rc == FL_RESULT_ACCES) {
            fprintf(stderr,
                    accept ? "bridge: only %s or an account above it can accept #%lld\n"
                           : "bridge: only %s, the other side or an account above either can revoke #%lld\n",
                    accept ? b.b_key : b.a_key, (long long)id);
            return 1;
        }
        if (rc == FL_RESULT_BUSY) {
            fprintf(stderr, "bridge: #%lld is already active\n", (long long)id);
            return 1;
        }
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "bridge: %s failed (%d)\n", argv[1], (int)rc);
            return 1;
        }
        snprintf(detail, sizeof detail, "bridge=%lld", (long long)id);
        fl_audit_identity_event(actor, accept ? "bridge-accept" : "bridge-revoke",
                                strcmp(actor, b.a_key) ? b.a_key : b.b_key, detail);
        printf("bridge: #%lld %s (%s <-> %s)\n", (long long)id, accept ? "active" : "revoked",
               b.a_key, b.b_key);
        return 0;
    }

    /* A KeyQuorum private bridge (made or received by the keyquorum CLI) copied into the account store, for the
     * account that is logged in. It changes who may cross a bridge at su, so it needs an elevated session. The
     * store is the file `keyquorum private-bridge import` wrote; nothing here validates a package. */
    if (!strcmp(argv[1], "kq-import") && (argc == 4 || (argc == 5 && !strcmp(argv[4], "--roster-only")))) {
        int roster_only = argc == 5;

        if (!fl_session_has_elevation()) {
            fprintf(stderr, "bridge: kq-import changes who may cross a bridge; run it with sudo\n");
            return 1;
        }
        rc = fl_user_db_bridge_import_from_kq(db, argv[2], argv[3], roster_only ? NULL : actor);
        if (rc == FL_RESULT_NOENT) {
            fprintf(stderr, roster_only ? "bridge: %s has no private bridge %s\n"
                                        : "bridge: %s has no private bridge %s with a sealed key for %s\n",
                    argv[2], argv[3], actor);
            return 1;
        }
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "bridge: kq-import failed (%d); the KeyQuorum store was not usable or the bridge was malformed\n", (int)rc);
            return 1;
        }
        fl_audit_identity_event(actor, "bridge-kq-import", argv[3], roster_only ? "roster" : "with-key");
        printf("bridge: imported private bridge %s%s\n", argv[3], roster_only ? " (roster only: can verify, cannot prove)" : "");
        return 0;
    }

    if (!strcmp(argv[1], "assembly") && (argc == 3 || (argc == 4 && !strcmp(argv[3], "--reissue"))))
        return bridge_assembly(db, actor, argv[2], argc == 4);

    bridge_usage();
    return 1;
}

int cmd_bridge_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 1 >= argc || !argv[i + 1] || cmd_batch_token_is_shell_command(argv[i + 1]))
        return 1;
    if (!strcmp(argv[i + 1], "list"))
        return 2;
    if (!strcmp(argv[i + 1], "kq-import")) {
        /* bridge kq-import <keyquorum-store> <uid> [--roster-only]: up to four arguments' worth of tokens, and
         * never past a token that starts the next shell command. */
        int n = 2;

        if (i + 2 < argc && argv[i + 2] && !cmd_batch_token_is_shell_command(argv[i + 2]))
            n++;
        if (n == 3 && i + 3 < argc && argv[i + 3] && !cmd_batch_token_is_shell_command(argv[i + 3]))
            n++;
        if (n == 4 && i + 4 < argc && argv[i + 4] && !strcmp(argv[i + 4], "--roster-only"))
            n++;
        return n;
    }
    if (!strcmp(argv[i + 1], "assembly")) {
        /* bridge assembly <parent> [--reissue]: the verb, the account, and the flag when it follows. */
        int n = 2;

        if (i + 2 < argc && argv[i + 2] && !cmd_batch_token_is_shell_command(argv[i + 2]))
            n++;
        if (n == 3 && i + 3 < argc && argv[i + 3] && !strcmp(argv[i + 3], "--reissue"))
            n++;
        return n;
    }
    if (i + 2 < argc && argv[i + 2] && !cmd_batch_token_is_shell_command(argv[i + 2]))
        return 3;
    return 2;
}
