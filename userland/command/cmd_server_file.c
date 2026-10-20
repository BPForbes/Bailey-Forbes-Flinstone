#include "cmd_server_file.h"

#include "cmd_authutil.h"
#include "common.h"
#include "fl/mem_asm.h"
#include "fl_colors.h"
#include "kq_checkpoint.h"
#include "net_kq_delivery.h"
#include "timekeeping.h"
#include "net_file_delivery.h"
#include "server_file_expire.h"
#include "server_shared_db.h"
#include "server_shared_fs.h"
#include "session.h"
#include "share_gate.h"

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
    return fl_server_file_parse_expire_arg(arg, expires_at_out);
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
    uint64_t file_size = 0u;
    fl_server_file_offer_t offer;
    fl_result_t rc;
    int i;

    if (!session_active(ctx, &hosting))
        return 1;

    for (i = 0; i < argc; i++) {
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
        fl_color_error("usage: server file [-all | -user <name> [-id <N>] | -id <N>] <path> [-vweor...] [-Ex<duration|mm-dd-yyyy [hh:mm[:ss]]>]");
        return 1;
    }
    if (public_offer && (target_name || target_id != 0u)) {
        fl_color_error("server file: -all is mutually exclusive with -user/-id");
        return 1;
    }
    if (stat_local_file(path, &file_size) != 0) {
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
        /*
         * Before anything leaves, the sender checks that the receiver's account
         * is on its own line or joined to it by a signed, active bridge
         * (share_gate.h). The server does not repeat this check.
         */
        {
            char receiver[FL_SERVER_PRINCIPAL_MAX];
            fl_share_gate_result_t gate;

            if (fl_server_file_member_principal(receiver_id, receiver, sizeof receiver) !=
                FL_RESULT_OK) {
                pthread_mutex_unlock(ctx->mutex);
                fl_color_warn("file offer refused: the receiver has no account name");
                return 1;
            }
            gate = fl_share_gate_check(fl_session_user_db(), current_principal(), receiver);
            if (gate != FL_SHARE_GATE_OK) {
                pthread_mutex_unlock(ctx->mutex);
                fl_color_warn("file offer to %s refused: %s", receiver, fl_share_gate_reason(gate));
                return 1;
            }
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
    offer.file_size = file_size;
    offer.chunk_size = FL_SERVER_FILE_CHUNK_MAX;
    offer.total_chunks = offer.file_size == 0u ? 0u :
        (uint32_t)((offer.file_size + offer.chunk_size - 1u) / offer.chunk_size);

    rc = fl_net_file_send_offer(ctx->server, ctx->client, hosting,
                                sender_member_id(ctx, hosting), &offer);
    if (rc == FL_RESULT_OK)
        rc = fl_net_file_send_file_contents(ctx->server, ctx->client, hosting,
                                            sender_member_id(ctx, hosting),
                                            &offer, path);
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
    if (argc < 2) {
        fl_color_error("usage: server file accept <share_id> [--overwrite | --server-share]");
        return 1;
    }
    share_id = argv[1];
    for (i = 2; i < argc; i++) {
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
    if (argc < 2) {
        fl_color_error("usage: server file decline <share_id>");
        return 1;
    }
    share_id = argv[1];
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
    if (argc < 2) {
        fl_color_error("usage: server file revoke <share_id>");
        return 1;
    }
    share_id = argv[1];
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

static int verb_file_fetch(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int hosting = 0;
    fl_server_catalog_entry_t meta;
    uint8_t *buf = NULL;
    size_t len = 0;
    uint64_t now = (uint64_t)time(NULL);
    fl_result_t rc;
    int ret = 1;

    if (argc < 2 || !argv[1] || !argv[1][0]) {
        fl_color_error("usage: server file fetch <content_hash>");
        return 1;
    }
    if (!session_active(ctx, &hosting))
        return 1;
    rc = fl_server_catalog_fetch_by_hash(argv[1],
                                         sender_member_id(ctx, hosting),
                                         now, &meta, NULL, 0, NULL);
    if (rc != FL_RESULT_OK) {
        fl_color_error("fetch failed (rc=%d)", (int)rc);
        return 1;
    }
    if (meta.total_bytes > FL_SERVER_CATALOG_MAX_BYTES) {
        fl_color_error("catalog blob too large (%llu bytes)",
                         (unsigned long long)meta.total_bytes);
        return 1;
    }
    if (meta.total_bytes > 0u) {
        buf = (uint8_t *)malloc((size_t)meta.total_bytes + 1u);
        if (!buf) {
            fl_color_error("out of memory for %llu-byte fetch",
                           (unsigned long long)meta.total_bytes);
            return 1;
        }
        rc = fl_server_catalog_read_blob(&meta, buf, (size_t)meta.total_bytes, &len);
        if (rc != FL_RESULT_OK) {
            fl_color_error("fetch read failed (rc=%d)", (int)rc);
            free(buf);
            return 1;
        }
        buf[len] = '\0';
    }
    if (meta.payload_kind == FL_CHANNEL_PAYLOAD_MSG) {
        printf("[catalog msg %s] %.*s\n", meta.transfer_id, (int)len,
               len > 0u ? (const char *)buf : "");
        ret = 0;
    } else {
        char out_path[512];
        fl_server_file_offer_t offer;
        memset(&offer, 0, sizeof(offer));
        strncpy(offer.share_id, meta.transfer_id, sizeof(offer.share_id) - 1u);
        strncpy(offer.file_name, meta.payload_name, sizeof(offer.file_name) - 1u);
        offer.file_perms = meta.file_perms;
        rc = fl_server_shared_save_offer(&offer, buf, len, out_path, sizeof(out_path));
        if (rc != FL_RESULT_OK)
            fl_color_error("could not land fetched file (rc=%d)", (int)rc);
        else {
            fl_color_success("fetched hash %s -> %s (%zu bytes)", argv[1],
                             meta.payload_name, len);
            ret = 0;
        }
    }
    free(buf);
    return ret;
}

/* ---- KeyQuorum co-signed delivery (contracts/drivers/contract_p4_keyquorum.h) ---- */

/* How long a shell waits for the host to answer a key lookup or release a letter. */
#define KQ_WAIT_KEYS_MS 3000u
#define KQ_WAIT_RELEASE_MS 10000u

static const fl_user_db_t *kq_users(void)
{
    return fl_session_user_db();
}

/* Opens this account's keys (cmd_open_account_keys) with server file's messages. */
static int kq_open_keys(const char *why, unsigned char *signing, unsigned char *box)
{
    fl_result_t rc = cmd_open_account_keys(why, signing, box);

    if (rc == FL_RESULT_OK)
        return 0;
    if (rc == FL_RESULT_EOF)
        fl_color_error("keyquorum: password read failed");
    else if (rc == FL_RESULT_ACCES)
        fl_color_error("keyquorum: incorrect password");
    else
        fl_color_error("keyquorum: cannot open the keys of %s (%d)", fl_session_current_user(),
                       (int)rc);
    return -1;
}

static void kq_utc_now(char *out, size_t cap)
{
    int64_t sec = 0;
    time_t t;
    struct tm tm;

    if (fl_time_wall_sec(&sec) != FL_RESULT_OK)
        sec = (int64_t)time(NULL);
    t = (time_t)sec;
    gmtime_r(&t, &tm);
    strftime(out, cap, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
}

/* server file send -t <path> -user <name>: encode checkpoint, then OFFER + parts. */
static int verb_kq_send(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int hosting = 0;
    const char *target = NULL;
    const char *path = NULL;
    const char *me = fl_session_current_key_id();
    fl_net_kq_keys_t keys;
    unsigned char signing[32];
    char created_at[40];
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    fl_kq_encoded_t enc;
    fl_kq_cp_result_t cp;
    fl_result_t rc;
    int i;

    if (!session_active(ctx, &hosting))
        return 1;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-t"))
            continue;
        if (!strcmp(argv[i], "-user") && i + 1 < argc)
            target = argv[++i];
        else if (argv[i][0] != '-' && !path)
            path = argv[i];
        else {
            fl_color_error("server file send -t: unexpected argument '%s'", argv[i]);
            return 1;
        }
    }
    if (!path || !target || !me || !me[0]) {
        fl_color_error("usage: server file send -t <path> -user <name>");
        return 1;
    }
    /* The same sender-side gate as any offer: same line, or a signed bridge. */
    {
        fl_share_gate_result_t gate =
            fl_share_gate_check(fl_session_user_db(), current_principal(), target);
        if (gate != FL_SHARE_GATE_OK) {
            fl_color_warn("co-signed delivery to %s refused: %s", target,
                          fl_share_gate_reason(gate));
            return 1;
        }
    }
    rc = fl_net_kq_lookup(ctx->server, ctx->client, hosting, target, KQ_WAIT_KEYS_MS, &keys);
    if (rc != FL_RESULT_OK) {
        fl_color_warn("co-signed delivery to %s refused: %s", target,
                      rc == FL_RESULT_TIMEDOUT ? "host did not answer the key lookup"
                                               : fl_kq_cp_reason(FL_KQ_CP_NO_KEY));
        return 1;
    }
    if (kq_open_keys("signs the revision", signing, NULL) != 0)
        return 1;
    kq_utc_now(created_at, sizeof created_at);
    cp = fl_kq_cp_encode(kq_users(), me, signing, keys.key_id, keys.box_public, path, created_at,
                         &enc);
    asm_mem_secure_zero(signing, sizeof signing);
    if (cp != FL_KQ_CP_OK) {
        fl_color_warn("co-signed delivery refused: %s", fl_kq_cp_reason(cp));
        return 1;
    }
    pthread_mutex_lock(ctx->mutex);
    rc = fl_net_kq_send_delivery(ctx->server, ctx->client, hosting, me, keys.key_id, &enc);
    pthread_mutex_unlock(ctx->mutex);
    fl_kq_delivery_id_hex(enc.delivery_id, hex);
    if (rc != FL_RESULT_OK)
        fl_color_warn("co-signed delivery failed to send (%d)", (int)rc);
    else
        printf("%s signed and sealed to %s; offered to the host as delivery %s "
               "(released once they co-sign)\n",
               enc.file_name, keys.key_id, hex);
    fl_kq_encoded_free(&enc);
    return rc == FL_RESULT_OK ? 0 : 1;
}

/* Decode checkpoint for a released letter, then ACK. Returns 0 when the file was written. */
static int kq_decode_and_ack(const cmd_server_ctx_t *ctx, int hosting,
                             const fl_net_kq_inbox_t *in, const unsigned char *box)
{
    fl_net_kq_keys_t sender;
    fl_kq_buf_t letter = {0};
    char dest[CWD_MAX + 160];
    fl_kq_cp_result_t cp;
    fl_result_t rc;

    rc = fl_net_kq_take_letter(in->id, 0, &letter);
    if (rc != FL_RESULT_OK)
        return -1;
    rc = fl_net_kq_lookup(ctx->server, ctx->client, hosting, in->sender, KQ_WAIT_KEYS_MS, &sender);
    if (rc != FL_RESULT_OK || strcmp(sender.key_id, in->sender) != 0) {
        fl_kq_buf_free(&letter);
        fl_color_warn("keyquorum: cannot get %s's key from the host", in->sender);
        return -1;
    }
    if (!fl_kq_cp_safe_name(in->file_name)) {
        fl_kq_buf_free(&letter);
        fl_color_warn("keyquorum: refused a delivery whose file name is not a plain name");
        return -1;
    }
    snprintf(dest, sizeof dest, "%s/%s", g_cwd, in->file_name);
    cp = fl_kq_cp_decode(kq_users(), in->sender, sender.signing_public,
                         fl_session_current_key_id(), box, &letter, &in->offer, in->sig, dest);
    fl_kq_buf_free(&letter);
    if (cp == FL_KQ_CP_EXISTS || cp == FL_KQ_CP_IO) {
        /* A local problem with the destination, not with the letter: keep it to retry. */
        char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
        fl_kq_delivery_id_hex(in->id, hex);
        fl_color_warn("keyquorum: %s not saved: %s; change directory and run: server file open %s",
                      in->file_name, fl_kq_cp_reason(cp), hex);
        return 1;
    }
    pthread_mutex_lock(ctx->mutex);
    (void)fl_net_kq_send_ack(ctx->server, ctx->client, hosting, in->id, cp == FL_KQ_CP_OK);
    pthread_mutex_unlock(ctx->mutex);
    if (cp != FL_KQ_CP_OK) {
        fl_color_warn("keyquorum: %s from %s refused: %s", in->file_name, in->sender,
                      fl_kq_cp_reason(cp));
        fl_net_kq_inbox_drop(in->id);
        return 1;
    }
    printf("%s from %s decoded and verified; saved to %s\n", in->file_name, in->sender, dest);
    fl_net_kq_inbox_drop(in->id);
    return 0;
}

static int kq_find_inbox(const char *hex, fl_net_kq_inbox_t *out)
{
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    fl_net_kq_inbox_t all[16];
    size_t n;

    if (fl_kq_delivery_id_parse(hex, id) != 0) {
        fl_color_error("keyquorum: '%s' is not a delivery id", hex);
        return -1;
    }
    n = fl_net_kq_inbox(all, 16);
    for (size_t i = 0; i < n; i++) {
        if (memcmp(all[i].id, id, sizeof id) == 0) {
            *out = all[i];
            return 0;
        }
    }
    fl_color_error("keyquorum: no co-signature request %s (run: server file cosign)", hex);
    return -1;
}

/* server file cosign [<delivery>]: list requests, or co-sign one and decode its release. */
static int verb_kq_cosign(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int hosting = 0;
    fl_net_kq_inbox_t in;
    unsigned char signing[32];
    unsigned char box[32];
    uint8_t sig[64];
    fl_kq_cp_result_t cp;
    fl_kq_buf_t probe = {0};
    int rc;

    if (!session_active(ctx, &hosting))
        return 1;
    if (argc < 2) {
        fl_net_kq_inbox_t all[16];
        size_t n;
        char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];

        pthread_mutex_lock(ctx->mutex);
        (void)fl_net_kq_pull(ctx->server, ctx->client, hosting);
        pthread_mutex_unlock(ctx->mutex);
        n = fl_net_kq_inbox(all, 16);
        if (n == 0)
            printf("(no co-signature requests)\n");
        for (size_t i = 0; i < n; i++) {
            fl_kq_delivery_id_hex(all[i].id, hex);
            printf("%s  %s  from %s  %s\n", hex, all[i].file_name, all[i].sender,
                   all[i].letter_ready ? "released" : all[i].cosigned ? "co-signed" : "waiting for you");
        }
        return 0;
    }
    if (kq_find_inbox(argv[1], &in) != 0)
        return 1;
    if (kq_open_keys("co-signs the delivery", signing, box) != 0)
        return 1;
    cp = fl_kq_cp_cosign(&in.offer, fl_session_current_key_id(), signing, sig);
    asm_mem_secure_zero(signing, sizeof signing);
    if (cp != FL_KQ_CP_OK) {
        asm_mem_secure_zero(box, sizeof box);
        fl_color_warn("keyquorum: cannot co-sign: %s", fl_kq_cp_reason(cp));
        return 1;
    }
    pthread_mutex_lock(ctx->mutex);
    rc = fl_net_kq_send_cosign(ctx->server, ctx->client, hosting, in.id,
                               fl_session_current_key_id(), sig) == FL_RESULT_OK ? 0 : -1;
    pthread_mutex_unlock(ctx->mutex);
    if (rc != 0) {
        /* Not recorded: the request stays waiting, so cosign can simply run again. */
        asm_mem_secure_zero(box, sizeof box);
        fl_color_warn("keyquorum: the co-signature could not be sent to the host; run cosign again");
        return 1;
    }
    (void)fl_net_kq_inbox_set_cosig(in.id, sig);
    memcpy(in.sig, sig, sizeof sig);
    in.cosigned = 1;
    /* The host fires on our co-signature; decode as soon as the letter arrives. */
    if (fl_net_kq_take_letter(in.id, KQ_WAIT_RELEASE_MS, &probe) != FL_RESULT_OK) {
        char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
        asm_mem_secure_zero(box, sizeof box);
        fl_kq_delivery_id_hex(in.id, hex);
        printf("co-signed; when the host releases it, run: server file open %s\n", hex);
        return 0;
    }
    fl_kq_buf_free(&probe);
    rc = kq_decode_and_ack(ctx, hosting, &in, box);
    asm_mem_secure_zero(box, sizeof box);
    return rc == 0 ? 0 : 1;
}

/* server file open <delivery>: decode a released letter this shell already co-signed. */
static int verb_kq_open(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int hosting = 0;
    fl_net_kq_inbox_t in;
    unsigned char box[32];
    fl_kq_buf_t probe = {0};
    int rc;

    if (!session_active(ctx, &hosting))
        return 1;
    if (argc < 2) {
        fl_color_error("usage: server file open <delivery>");
        return 1;
    }
    if (kq_find_inbox(argv[1], &in) != 0)
        return 1;
    if (!in.cosigned) {
        fl_color_error("keyquorum: co-sign it first: server file cosign %s", argv[1]);
        return 1;
    }
    /* Send the co-signature again (the host releases a fired delivery again and never fires
     * twice), so a co-signature lost on the way still reaches it, then pull. */
    pthread_mutex_lock(ctx->mutex);
    (void)fl_net_kq_send_cosign(ctx->server, ctx->client, hosting, in.id,
                                fl_session_current_key_id(), in.sig);
    (void)fl_net_kq_pull(ctx->server, ctx->client, hosting);
    pthread_mutex_unlock(ctx->mutex);
    if (fl_net_kq_take_letter(in.id, KQ_WAIT_RELEASE_MS, &probe) != FL_RESULT_OK) {
        fl_color_warn("keyquorum: the host has not released %s yet", argv[1]);
        return 1;
    }
    fl_kq_buf_free(&probe);
    if (kq_open_keys("opens the delivery", NULL, box) != 0)
        return 1;
    rc = kq_decode_and_ack(ctx, hosting, &in, box);
    asm_mem_secure_zero(box, sizeof box);
    return rc == 0 ? 0 : 1;
}

int cmd_server_file_run(const cmd_server_ctx_t *ctx, int argc, char **argv)
{
    int sub_argc;
    char **sub_argv;

    if (argc < 3) {
        fl_color_error("usage: server file <send [-t]|inbox|public|sent|accept|decline|revoke|fetch|cosign|open> ...");
        return 1;
    }
    sub_argc = argc - 2;
    sub_argv = argv + 2;
    if (sub_argc <= 0) {
        fl_color_error("usage: server file <send [-t]|inbox|public|sent|accept|decline|revoke|fetch|cosign|open> ...");
        return 1;
    }
    if (!strcmp(argv[2], "inbox"))
        return verb_file_list(ctx, "inbox");
    if (!strcmp(argv[2], "public"))
        return verb_file_list(ctx, "public");
    if (!strcmp(argv[2], "sent"))
        return verb_file_list(ctx, "sent");
    if (!strcmp(argv[2], "accept"))
        return verb_file_accept(ctx, sub_argc, sub_argv);
    if (!strcmp(argv[2], "decline"))
        return verb_file_decline(ctx, sub_argc, sub_argv);
    if (!strcmp(argv[2], "revoke"))
        return verb_file_revoke(ctx, sub_argc, sub_argv);
    if (!strcmp(argv[2], "fetch"))
        return verb_file_fetch(ctx, sub_argc, sub_argv);
    if (!strcmp(argv[2], "cosign"))
        return verb_kq_cosign(ctx, sub_argc, sub_argv);
    if (!strcmp(argv[2], "open"))
        return verb_kq_open(ctx, sub_argc, sub_argv);
    for (int i = 0; i < sub_argc; i++)
        if (!strcmp(sub_argv[i], "-t"))
            return verb_kq_send(ctx, sub_argc - (strcmp(argv[2], "send") ? 0 : 1),
                                sub_argv + (strcmp(argv[2], "send") ? 0 : 1));
    return verb_file_send(ctx, sub_argc, sub_argv);
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
