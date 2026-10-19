#include "cmd_decl.h"
#include "cmd_batch.h"
#include "fl/session.h"
#include "bridge.h"
#include "fs_jail.h"
#include "heritage.h"
#include "user_db.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char *subject;
    unsigned    shown;
} profile_bridge_ctx_t;

static void profile_print_bridge(const fl_bridge_t *b, void *ctx_v) {
    profile_bridge_ctx_t *ctx = ctx_v;
    const char *peer;

    if (strcmp(b->a_key, ctx->subject) != 0 && strcmp(b->b_key, ctx->subject) != 0)
        return;
    peer = strcmp(b->a_key, ctx->subject) == 0 ? b->b_key : b->a_key;
    printf("  bridge #%lld  %-8s  %s%s\n", (long long)b->id,
           b->state == FL_BRIDGE_STATE_ACTIVE ? "active" : "pending", peer,
           b->state == FL_BRIDGE_STATE_PENDING && strcmp(b->a_key, ctx->subject) == 0
               ? " (waiting for them)"
               : (b->state == FL_BRIDGE_STATE_PENDING ? " (waiting for you)" : ""));
    ctx->shown++;
}

static void profile_print_time(const char *label, int64_t when) {
    char buf[32];
    time_t t = (time_t)when;
    struct tm tmv;

    if (when <= 0 || !gmtime_r(&t, &tmv) || !strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%SZ", &tmv)) {
        printf("%-12s unknown\n", label);
        return;
    }
    printf("%-12s %s\n", label, buf);
}

int cmd_profile_run(int argc, char **argv) {
    const fl_user_db_t *db;
    const fl_user_record_t *u;
    char actor[FL_HERITAGE_KEY_ID_MAX];
    char line[FL_HERITAGE_KEY_ID_MAX];
    fl_heritage_relation_t rel;
    size_t i;
    unsigned children = 0;

    fl_session_init();
    db = fl_session_user_db();
    u = fl_user_db_find(db, argc >= 2 ? argv[1] : fl_session_current_user());
    if (!u) {
        fprintf(stderr, "profile: %s does not exist\n", argc >= 2 ? argv[1] : "current account");
        return 1;
    }
    strncpy(actor, fl_session_current_key_id(), sizeof(actor) - 1);
    actor[sizeof(actor) - 1] = '\0';
    rel = fl_heritage_relation(actor, u->key_id);

    printf("%-12s %s\n", "account", u->name);
    printf("%-12s %s\n", "key id", u->key_id[0] ? u->key_id : "(none: name is not a valid segment)");
    /* Heritage line, Root first: Root -> Root.Flinstone -> Root.Flinstone.Bob */
    if (fl_heritage_key_valid(u->key_id)) {
        const char *p;

        fputs("heritage     ", stdout);
        for (p = u->key_id;; p++) {
            if (*p == FL_HERITAGE_SEP || *p == '\0') {
                size_t len = (size_t)(p - u->key_id);

                memcpy(line, u->key_id, len);
                line[len] = '\0';
                printf("%s%s", line, *p ? " -> " : "\n");
                if (!*p)
                    break;
            }
        }
    }
    printf("%-12s %s\n", "created by", u->created_by[0] ? u->created_by : "(master account)");
    profile_print_time("created", u->created_at);
    printf("%-12s %u%s\n", "uid", (unsigned)u->uid, u->is_elevated ? " (elevated account)" : "");
    if (actor[0] && rel != FL_HERITAGE_REL_SELF)
        printf("%-12s %s (from %s)\n", "relation", fl_heritage_relation_name(rel), actor);
    for (i = 0; i < db->count; i++) {
        char parent[FL_HERITAGE_KEY_ID_MAX];

        if (fl_heritage_parent(db->users[i].key_id, parent, sizeof parent) == FL_RESULT_OK &&
            strcmp(parent, u->key_id) == 0) {
            printf("%-12s %s\n", children ? "" : "created", db->users[i].key_id);
            children++;
        }
    }
    if (fs_jail_is_active() && fl_heritage_key_valid(u->key_id)) {
        char home[4096];

        if (fs_jail_account_home(u->key_id, home, sizeof home) == 0)
            printf("%-12s %s\n", "home", home);
    } else if (fl_heritage_home_rel(u->key_id, line, sizeof line) == FL_RESULT_OK) {
        printf("%-12s <jail>/%s (jail inactive)\n", "home", line);
    }
    /* Bridges are shown to the account itself and to its ancestors only. */
    if (rel == FL_HERITAGE_REL_SELF || rel == FL_HERITAGE_REL_DESCENDANT) {
        profile_bridge_ctx_t ctx = {u->key_id, 0};

        printf("bridges\n");
        if (fl_bridge_list_visible(db->path, actor, profile_print_bridge, &ctx) != FL_RESULT_OK)
            printf("  (bridge store unavailable)\n");
        else if (!ctx.shown)
            printf("  none\n");
    }
    return 0;
}

int cmd_profile_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-' &&
        !cmd_batch_token_is_shell_command(argv[i + 1]))
        return 2;
    return 1;
}
