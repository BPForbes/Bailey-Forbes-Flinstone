#include "cmd_server_file.h"

#include "fl_colors.h"
#include "net_file_delivery.h"
#include "session.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *current_principal(void)
{
    const char *u = fl_session_current_user();
    if (u && u[0])
        return u;
    return "Flinstone";
}

static fl_net_server_member_id_t sender_member_id(const cmd_server_ctx_t *ctx,
                                                  int hosting)
{
    if (hosting)
        return FL_NET_SERVER_MEMBER_ID_HOST;
    return ctx->client->assigned_member_id;
}

static void populate_member_lookup(const cmd_server_ctx_t *ctx, int hosting)
{
    fl_server_file_lookup_begin();
    if (hosting) {
        size_t count = fl_net_server_member_count(ctx->server);
        for (size_t i = 0; i < count; i++) {
            const fl_net_server_member_t *m =
                fl_net_server_member_at(ctx->server, i);
            fl_server_file_member_ref_t ref;
            if (!m)
                continue;
            ref.member_id = m->member_id;
            ref.disambig_index = m->disambig_index;
            strncpy(ref.principal, m->principal, sizeof(ref.principal) - 1u);
            ref.principal[sizeof(ref.principal) - 1u] = '\0';
            strncpy(ref.nick, m->nick, sizeof(ref.nick) - 1u);
            ref.nick[sizeof(ref.nick) - 1u] = '\0';
            (void)fl_server_file_lookup_push(&ref);
        }
        return;
    }
    for (size_t i = 0; i < ctx->client->member_count; i++) {
        const fl_net_client_member_t *m = fl_net_client_member_at(ctx->client, i);
        fl_server_file_member_ref_t ref;
        if (!m)
            continue;
        ref.member_id = m->member_id;
        ref.disambig_index = m->disambig_index;
        strncpy(ref.principal, m->principal, sizeof(ref.principal) - 1u);
        ref.principal[sizeof(ref.principal) - 1u] = '\0';
        strncpy(ref.nick, m->nick, sizeof(ref.nick) - 1u);
        ref.nick[sizeof(ref.nick) - 1u] = '\0';
        (void)fl_server_file_lookup_push(&ref);
    }
}

static int session_active(const cmd_server_ctx_t *ctx, int *hosting_out)
{
    int hosting = ctx->server_running && *ctx->server_running;
    int joined = fl_net_client_state(ctx->client) == FL_NET_CLIENT_STATE_CONNECTED;
    if (!hosting && !joined) {
        fl_color_error("not in a session; host or join first");
        return 0;
    }
    *hosting_out = hosting;
    return 1;
}

static fl_file_perms_t parse_perm_flags(const char *flag)
{
    fl_file_perms_t perms = 0u;
    if (!flag)
        return perms;
    for (const char *p = flag; *p; p++) {
        switch (*p) {
        case 'v': perms |= FL_FILE_PERM_VIEW; break;
        case 'w': perms |= FL_FILE_PERM_WRITE; break;
        case 'e': perms |= FL_FILE_PERM_EDIT; break;
        case 'r': perms |= FL_FILE_PERM_RUN; break;
        case 'o': perms |= FL_FILE_PERM_OVERWRITE; break;
        default: break;
        }
    }
    if (perms & FL_FILE_PERM_WRITE)
        perms |= FL_FILE_PERM_SERVER_SHARE;
    return fl_file_perms_normalize(perms);
}

static int parse_expire_arg(const char *arg, uint64_t *expires_at_out)
{
    unsigned long n = 0;
    char unit = '\0';
    if (!arg || !expires_at_out)
        return -1;
    if (sscanf(arg, "%lu%c", &n, &unit) < 1)
        return -1;
    *expires_at_out = (uint64_t)time(NULL);
    switch (unit) {
    case 'h': case 'H': *expires_at_out += (uint64_t)n * 3600u; break;
    case 'm': case 'M': *expires_at_out += (uint64_t)n * 60u; break;
    case 'd': case 'D': *expires_at_out += (uint64_t)n * 86400u; break;
    default: *expires_at_out += (uint64_t)n; break;
    }
    return 0;
}

static int stat_local_file(const char *path, uint64_t *size_out)
{
    struct stat st;
    if (!path || !size_out)
        return -1;
    if (stat(path, &st) != 0)
        return -1;
    if (!S_ISREG(st.st_mode))
        return -1;
    *size_out = (uint64_t)st.st_size;
    return 0;
}

static int verb_file_send(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int hosting = 0;
    int public_offer = 0;
    const char *target_name = NULL;
    uint16_t target_id = 0u;
    const char *path = NULL;
    fl_file_perms_t perms = FL_FILE_PERM_VIEW;
    uint64_t expires_at = 0u;
    fl_server_file_offer_t offer;
    fl_result_t rc;
    int i;

    if (!session_active(ctx, &hosting))
        return 1;

    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-all")) {
            public_offer = 1;
        } else if (!strcmp(argv[i], "-user") && i + 1 < argc) {
            target_name = argv[++i];
        } else if (!strcmp(argv[i], "-id") && i + 1 < argc) {
            target_id = (uint16_t)atoi(argv[++i]);
        } else if (argv[i][0] == '-' && argv[i][1] == 'E' && argv[i][2]) {
            if (parse_expire_arg(argv[i] + 2, &expires_at) != 0) {
                fl_color_error("invalid expiration '%s'", argv[i]);
                return 1;
            }
        } else if (argv[i][0] == '-' && argv[i][1]) {
            perms |= parse_perm_flags(argv[i] + 1);
        } else if (!path) {
            path = argv[i];
        } else {
            fl_color_error("unexpected argument '%s'", argv[i]);
            return 1;
        }
    }

    if (!path) {
        fl_color_error("usage: server file [-all | -user <name> [-id <N>] | -id <N>] <path> [-vweor...] [-Ex<duration>]");
        return 1;
    }
    if (public_offer && (target_name || target_id != 0u)) {
        fl_color_error("server file: -all is mutually exclusive with -user/-id");
        return 1;
    }
    if (stat_local_file(path, &offer.file_size) != 0) {
        fl_color_error("cannot read file '%s'", path);
        return 1;
    }

    pthread_mutex_lock(ctx->mutex);
    populate_member_lookup(ctx, hosting);

    if (public_offer) {
        rc = fl_server_file_offer_create(&offer, sender_member_id(ctx, hosting), 0u,
                                         path, path, perms, expires_at);
    } else {
        uint16_t receiver_id = 0u;
        rc = fl_server_file_resolve_receiver(target_name, target_id,
                                             sender_member_id(ctx, hosting),
                                             &receiver_id);
        if (rc == FL_RESULT_BUSY) {
            pthread_mutex_unlock(ctx->mutex);
            fl_color_warn("file offer failed: ambiguous nickname");
            return 1;
        }
        if (rc != FL_RESULT_OK) {
            pthread_mutex_unlock(ctx->mutex);
            fl_color_warn("file offer failed: user not found");
            return 1;
        }
        rc = fl_server_file_offer_create(&offer, sender_member_id(ctx, hosting),
                                         receiver_id, path, path, perms, expires_at);
    }
    if (rc != FL_RESULT_OK) {
        pthread_mutex_unlock(ctx->mutex);
        fl_color_error("server file: offer create failed (rc=%d)", (int)rc);
        return 1;
    }

    (void)fl_server_file_snapshot_display_names(offer.sender_member_id,
                                                offer.receiver_member_id,
                                                offer.sender_display,
                                                (uint16_t)sizeof(offer.sender_display),
                                                offer.receiver_display,
                                                (uint16_t)sizeof(offer.receiver_display));
    strncpy(offer.sender_principal, current_principal(),
            sizeof(offer.sender_principal) - 1u);
    offer.sender_principal[sizeof(offer.sender_principal) - 1u] = '\0';
    offer.chunk_size = FL_SERVER_FILE_CHUNK_MAX;
    offer.total_chunks = offer.file_size == 0u ? 0u :
        (uint32_t)((offer.file_size + offer.chunk_size - 1u) / offer.chunk_size);

    rc = fl_net_file_send_offer(ctx->server, ctx->client, hosting,
                                sender_member_id(ctx, hosting), &offer);
    pthread_mutex_unlock(ctx->mutex);
    if (rc != FL_RESULT_OK) {
        fl_color_warn("file offer failed");
        return 1;
    }
    fl_color_success("file offer sent");
    return 0;
}

static int verb_file_list(const cmd_server_ctx_t *ctx, const char *which)
{
    int hosting = 0;
    fl_net_server_member_id_t mid;
    if (!session_active(ctx, &hosting))
        return 1;
    mid = sender_member_id(ctx, hosting);
    pthread_mutex_lock(ctx->mutex);
    if (!strcmp(which, "inbox"))
        (void)fl_server_share_inbox_list(mid);
    else if (!strcmp(which, "public"))
        (void)fl_server_share_public_list();
    else
        (void)fl_server_share_sent_list(mid);
    pthread_mutex_unlock(ctx->mutex);
    return 0;
}

static int verb_file_accept(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int hosting = 0;
    fl_server_file_disposition_t disp = FL_SERVER_FILE_DECLINE;
    const char *share_id = NULL;
    int i;

    if (!session_active(ctx, &hosting))
        return 1;
    if (argc < 3) {
        fl_color_error("usage: server file accept <share_id> [--overwrite | --server-share]");
        return 1;
    }
    share_id = argv[2];
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--overwrite"))
            disp = FL_SERVER_FILE_OVERWRITE_LOCAL;
        else if (!strcmp(argv[i], "--server-share"))
            disp = FL_SERVER_FILE_SAVE_TO_SERVER_SHARE;
    }
    if (disp == FL_SERVER_FILE_DECLINE) {
        fl_color_error("server file accept: choose --overwrite or --server-share");
        return 1;
    }

    pthread_mutex_lock(ctx->mutex);
    {
        uint8_t payload[FL_NET_SESSION_MAX_MSG];
        uint16_t plen = 0;
        fl_result_t rc = fl_file_packet_encode_accept(share_id,
                                                       sender_member_id(ctx, hosting),
                                                       disp, payload, sizeof(payload),
                                                       &plen);
        if (rc == FL_RESULT_OK)
            rc = fl_net_file_send_control(ctx->server, ctx->client, hosting,
                                          sender_member_id(ctx, hosting),
                                          FL_NET_SESSION_OP_FILE_ACCEPT,
                                          payload, plen);
        if (rc == FL_RESULT_OK)
            rc = fl_server_share_accept(share_id, sender_member_id(ctx, hosting), disp);
        pthread_mutex_unlock(ctx->mutex);
        if (rc != FL_RESULT_OK) {
            fl_color_error("server file accept failed (rc=%d)", (int)rc);
            return 1;
        }
    }
    fl_color_success("accept sent for %s", share_id);
    return 0;
}

static int verb_file_decline(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int hosting = 0;
    const char *share_id;
    if (!session_active(ctx, &hosting))
        return 1;
    if (argc < 3) {
        fl_color_error("usage: server file decline <share_id>");
        return 1;
    }
    share_id = argv[2];
    pthread_mutex_lock(ctx->mutex);
    {
        uint8_t payload[FL_NET_SESSION_MAX_MSG];
        uint16_t plen = 0;
        fl_result_t rc = fl_file_packet_encode_decline(share_id,
                                                       sender_member_id(ctx, hosting),
                                                       payload, sizeof(payload), &plen);
        if (rc == FL_RESULT_OK)
            rc = fl_net_file_send_control(ctx->server, ctx->client, hosting,
                                          sender_member_id(ctx, hosting),
                                          FL_NET_SESSION_OP_FILE_DECLINE,
                                          payload, plen);
        if (rc == FL_RESULT_OK)
            rc = fl_server_share_decline(share_id, sender_member_id(ctx, hosting));
        pthread_mutex_unlock(ctx->mutex);
        if (rc != FL_RESULT_OK) {
            fl_color_error("server file decline failed (rc=%d)", (int)rc);
            return 1;
        }
    }
    fl_color_success("declined %s", share_id);
    return 0;
}

static int verb_file_revoke(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int hosting = 0;
    const char *share_id;
    if (!session_active(ctx, &hosting))
        return 1;
    if (argc < 3) {
        fl_color_error("usage: server file revoke <share_id>");
        return 1;
    }
    share_id = argv[2];
    pthread_mutex_lock(ctx->mutex);
    {
        uint8_t payload[FL_NET_SESSION_MAX_MSG];
        uint16_t plen = 0;
        fl_result_t rc = fl_file_packet_encode_revoke(share_id,
                                                       sender_member_id(ctx, hosting),
                                                       payload, sizeof(payload), &plen);
        if (rc == FL_RESULT_OK)
            rc = fl_net_file_send_control(ctx->server, ctx->client, hosting,
                                          sender_member_id(ctx, hosting),
                                          FL_NET_SESSION_OP_FILE_REVOKE,
                                          payload, plen);
        if (rc == FL_RESULT_OK)
            rc = fl_server_share_revoke(share_id, sender_member_id(ctx, hosting));
        pthread_mutex_unlock(ctx->mutex);
        if (rc != FL_RESULT_OK) {
            fl_color_error("server file revoke failed (rc=%d)", (int)rc);
            return 1;
        }
    }
    fl_color_success("revoked %s", share_id);
    return 0;
}

int cmd_server_file_run(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    if (argc < 3) {
        fl_color_error("usage: server file <send|inbox|public|sent|accept|decline|revoke> ...");
        return 1;
    }
    if (!strcmp(argv[2], "inbox"))
        return verb_file_list(ctx, "inbox");
    if (!strcmp(argv[2], "public"))
        return verb_file_list(ctx, "public");
    if (!strcmp(argv[2], "sent"))
        return verb_file_list(ctx, "sent");
    if (!strcmp(argv[2], "accept"))
        return verb_file_accept(ctx, argc, argv);
    if (!strcmp(argv[2], "decline"))
        return verb_file_decline(ctx, argc, argv);
    if (!strcmp(argv[2], "revoke"))
        return verb_file_revoke(ctx, argc, argv);
    return verb_file_send(ctx, argc, argv);
}

int cmd_server_send_file_alias(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    char *file_argv[32];
    int n = 0;
    int i;
    int has_user = 0;

    if (argc < 4 || strcmp(argv[1], "send") != 0) {
        fl_color_error("usage: server send -file <path> -user <name> ...");
        return 1;
    }
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-user"))
            has_user = 1;
    }
    if (!has_user) {
        fl_color_error("server send -file requires -user <name>");
        return 1;
    }

    file_argv[n++] = argv[0];
    file_argv[n++] = (char *)"file";
    for (i = 2; i < argc && n < (int)(sizeof(file_argv) / sizeof(file_argv[0])); i++) {
        if (!strcmp(argv[i], "-file"))
            continue;
        file_argv[n++] = argv[i];
    }
    return cmd_server_file_run(ctx, n, file_argv);
}
