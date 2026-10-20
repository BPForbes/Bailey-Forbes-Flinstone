/**
 * test_kq_net.c - KeyQuorum co-signed delivery over the session wire, in process
 * (kernel/core/net/net_kq_delivery.c; contracts/drivers/contract_p4_keyquorum.h).
 *
 * A recording transport stands in for sockets: frames a client sends go to the host
 * handler as that member, and frames the host sends to Steve go to the client handler.
 * Mya sends plan.txt; the host announces; Steve co-signs; the host fires once and
 * releases; Steve decodes and acknowledges. Refusals: wrong co-signer, bad signature,
 * expired request, an offer from someone else's account.
 */
#include "net_kq_delivery.h"
#include "kq_checkpoint.h"
#include "fl/mem_asm.h"
#include "user_db.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

#define MYA "Root.Flinstone.Mya"
#define STEVE "Root.Flinstone.Steve"

/* ---- a fake server: three members, no sockets --------------------------------- */

static fl_net_server_member_t s_members[3];
static fl_net_server_t *s_srv = (fl_net_server_t *)&s_members; /* opaque to the module */

size_t fl_net_server_member_count(const fl_net_server_t *srv) {
    (void)srv;
    return 3;
}

const fl_net_server_member_t *fl_net_server_member_at(const fl_net_server_t *srv, size_t i) {
    (void)srv;
    return i < 3 ? &s_members[i] : NULL;
}

void fl_net_server_set_kq_hook(fl_net_server_kq_hook_fn hook) { (void)hook; }
void fl_net_client_set_kq_hook(fl_net_client_kq_hook_fn hook) { (void)hook; }

fl_result_t fl_net_session_send_frame(fl_net_sock_handle_t h, uint8_t op, const uint8_t *p,
                                      uint16_t n) {
    (void)h; (void)op; (void)p; (void)n;
    return FL_RESULT_OK;
}

/* ---- recording transport --------------------------------------------------------- */

static const fl_net_server_member_t *s_actor;
static int s_to_steve[0x80];
static int s_to_mya[0x80];
static char s_last_err[512];

static void host_send(fl_net_server_t *srv, const fl_net_server_member_t *to, uint8_t opcode,
                      const uint8_t *payload, uint16_t plen) {
    (void)srv;
    if (opcode == 0x7Fu) {
        size_t n = plen < sizeof s_last_err - 1 ? plen : sizeof s_last_err - 1;
        memcpy(s_last_err, payload, n);
        s_last_err[n] = '\0';
    }
    if (to == &s_members[2]) {
        s_to_steve[opcode & 0x7Fu]++;
        fl_net_kq_client_handle(NULL, opcode, payload, plen);
    } else if (to == &s_members[1]) {
        s_to_mya[opcode & 0x7Fu]++;
        if (opcode == FL_NET_SESSION_OP_KQ_KEYS)
            fl_net_kq_client_handle(NULL, opcode, payload, plen);
    }
}

static void client_send(fl_net_client_t *client, uint8_t opcode, const uint8_t *payload,
                        uint16_t plen) {
    (void)client;
    fl_net_kq_host_handle(s_srv, s_actor, opcode, payload, plen);
}

static int64_t s_clock = 1000000;
static int64_t fake_now(void) { return s_clock; }

/* ---- fixtures -------------------------------------------------------------------- */

static char s_dir[256];
static fl_user_db_t s_db;

static const fl_user_db_t *the_users(void) { return &s_db; }

static void rmrf(const char *path) {
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) {
        unlink(path);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char sub[PATH_MAX];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        rmrf(sub);
    }
    closedir(d);
    rmdir(path);
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fputs(text, f);
    return fclose(f);
}

static void member(int i, const char *principal, int is_host) {
    memset(&s_members[i], 0, sizeof s_members[i]);
    s_members[i].in_use = 1;
    s_members[i].is_host = (uint8_t)is_host;
    s_members[i].member_id = (fl_net_server_member_id_t)(i + 1);
    s_members[i].peer_handle = FL_NET_SOCK_INVALID;
    snprintf(s_members[i].principal, sizeof s_members[i].principal, "%.63s", principal);
}

static int encode_from_mya(const char *name, const char *text, fl_kq_encoded_t *enc) {
    char src[PATH_MAX];
    unsigned char sign[FL_USER_SIGNING_SECRET_LEN];
    int rc;

    snprintf(src, sizeof src, "%s/%s", s_dir, name);
    if (write_text(src, text) != 0 || fl_user_db_open_key(&s_db, "Mya", "pw-mya", sign) != FL_RESULT_OK)
        return -1;
    rc = fl_kq_cp_encode(&s_db, MYA, sign, STEVE, fl_user_db_box_public(&s_db, STEVE), src,
                         "2026-10-20T09:00:00.000Z", enc) == FL_KQ_CP_OK ? 0 : -1;
    asm_mem_secure_zero(sign, sizeof sign);
    return rc;
}

/* ---- tests ----------------------------------------------------------------------- */

static int test_keys_come_from_the_host_registry(void) {
    fl_net_kq_keys_t k;

    s_actor = &s_members[1];
    ASSERT(fl_net_kq_lookup(s_srv, NULL, 0, "Steve", 0, &k) == FL_RESULT_OK);
    ASSERT(k.found && strcmp(k.key_id, STEVE) == 0);
    ASSERT(memcmp(k.box_public, fl_user_db_box_public(&s_db, STEVE), 32) == 0);
    ASSERT(memcmp(k.signing_public, fl_user_db_signing_public(&s_db, STEVE), 32) == 0);
    ASSERT(fl_net_kq_lookup(s_srv, NULL, 0, "Nobody", 0, &k) == FL_RESULT_NOENT);
    return 0;
}

static int test_co_signed_delivery_over_the_wire(void) {
    fl_kq_encoded_t enc;
    fl_net_kq_inbox_t inbox[4];
    unsigned char steve_sign[FL_USER_SIGNING_SECRET_LEN];
    unsigned char steve_box[FL_USER_SIGNING_SECRET_LEN];
    uint8_t sig[64];
    fl_kq_buf_t letter = {0};
    char dest[PATH_MAX];
    int releases_before;

    ASSERT(encode_from_mya("plan.txt", "quarterly plan v1\n", &enc) == 0);

    /* Steve cannot send Mya's offer as his own. */
    s_actor = &s_members[2];
    ASSERT(fl_net_kq_send_delivery(s_srv, NULL, 0, MYA, STEVE, &enc) == FL_RESULT_OK);
    ASSERT(strstr(s_last_err, "not from this member's account"));
    ASSERT(fl_net_kq_inbox(inbox, 4) == 0);

    /* Mya sends; the host holds it and announces it to Steve only. */
    s_actor = &s_members[1];
    ASSERT(fl_net_kq_send_delivery(s_srv, NULL, 0, MYA, STEVE, &enc) == FL_RESULT_OK);
    ASSERT(s_to_steve[FL_NET_SESSION_OP_KQ_ANNOUNCE] == 1);
    ASSERT(s_to_mya[FL_NET_SESSION_OP_KQ_ANNOUNCE] == 0);
    ASSERT(fl_net_kq_inbox(inbox, 4) == 1);
    ASSERT(strcmp(inbox[0].sender, MYA) == 0 && strcmp(inbox[0].file_name, "plan.txt") == 0);
    ASSERT(!inbox[0].letter_ready);
    /* Nothing is released before Steve co-signs. */
    ASSERT(s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE] == 0);

    ASSERT(fl_user_db_open_key(&s_db, "Steve", "pw-steve", steve_sign) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_box(&s_db, "Steve", "pw-steve", steve_box) == FL_RESULT_OK);
    ASSERT(fl_kq_cp_cosign(&inbox[0].offer, STEVE, steve_sign, sig) == FL_KQ_CP_OK);

    /* Mya's member cannot co-sign for Steve; a changed signature is refused. */
    s_actor = &s_members[1];
    ASSERT(fl_net_kq_send_cosign(s_srv, NULL, 0, inbox[0].id, STEVE, sig) == FL_RESULT_OK);
    ASSERT(strstr(s_last_err, "not addressed to this account"));
    s_actor = &s_members[2];
    sig[3] ^= 1u;
    ASSERT(fl_net_kq_send_cosign(s_srv, NULL, 0, inbox[0].id, STEVE, sig) == FL_RESULT_OK);
    ASSERT(strstr(s_last_err, "signature does not verify"));
    ASSERT(s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE] == 0);
    sig[3] ^= 1u;

    /* The real co-signature reaches threshold: the host fires and releases. */
    ASSERT(fl_net_kq_send_cosign(s_srv, NULL, 0, inbox[0].id, STEVE, sig) == FL_RESULT_OK);
    ASSERT(s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE] == 1);
    ASSERT(fl_net_kq_inbox_set_cosig(inbox[0].id, sig) == FL_RESULT_OK);
    ASSERT(fl_net_kq_take_letter(inbox[0].id, 0, &letter) == FL_RESULT_OK);
    ASSERT(letter.len == enc.letter.len && memcmp(letter.data, enc.letter.data, letter.len) == 0);

    /* Decode with the released letter. */
    snprintf(dest, sizeof dest, "%s/steve-plan.txt", s_dir);
    ASSERT(fl_kq_cp_decode(&s_db, MYA, fl_user_db_signing_public(&s_db, MYA), STEVE, steve_box,
                           &letter, &inbox[0].offer, sig, dest) == FL_KQ_CP_OK);
    fl_kq_buf_free(&letter);

    /* A repeated co-signature re-sends the letter but never fires again. */
    releases_before = s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE];
    ASSERT(fl_net_kq_send_cosign(s_srv, NULL, 0, inbox[0].id, STEVE, sig) == FL_RESULT_OK);
    ASSERT(s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE] == releases_before + 1);

    /* Acknowledged: the host deletes the letter and stops re-sending it. */
    ASSERT(fl_net_kq_send_ack(s_srv, NULL, 0, inbox[0].id, 1) == FL_RESULT_OK);
    releases_before = s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE];
    ASSERT(fl_net_kq_send_cosign(s_srv, NULL, 0, inbox[0].id, STEVE, sig) == FL_RESULT_OK);
    ASSERT(s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE] == releases_before);
    fl_net_kq_inbox_drop(inbox[0].id);

    asm_mem_secure_zero(steve_sign, sizeof steve_sign);
    asm_mem_secure_zero(steve_box, sizeof steve_box);
    fl_kq_encoded_free(&enc);
    return 0;
}

static int test_an_unanswered_request_leaks_away(void) {
    fl_kq_encoded_t enc;
    fl_net_kq_inbox_t inbox[4];
    unsigned char steve_sign[FL_USER_SIGNING_SECRET_LEN];
    uint8_t sig[64];
    int releases_before = s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE];

    ASSERT(encode_from_mya("late.txt", "too late\n", &enc) == 0);
    s_actor = &s_members[1];
    ASSERT(fl_net_kq_send_delivery(s_srv, NULL, 0, MYA, STEVE, &enc) == FL_RESULT_OK);
    ASSERT(fl_net_kq_inbox(inbox, 4) == 1);
    ASSERT(fl_user_db_open_key(&s_db, "Steve", "pw-steve", steve_sign) == FL_RESULT_OK);
    ASSERT(fl_kq_cp_cosign(&inbox[0].offer, STEVE, steve_sign, sig) == FL_KQ_CP_OK);

    /* The host's clock passes the expiry before Steve answers. */
    s_clock += FL_KQ_DELIVERY_TTL_SEC + 1;
    s_actor = &s_members[2];
    ASSERT(fl_net_kq_send_cosign(s_srv, NULL, 0, inbox[0].id, STEVE, sig) == FL_RESULT_OK);
    ASSERT(strstr(s_last_err, "no such delivery") || strstr(s_last_err, "expired"));
    ASSERT(s_to_steve[FL_NET_SESSION_OP_KQ_RELEASE] == releases_before);
    fl_net_kq_inbox_drop(inbox[0].id);
    asm_mem_secure_zero(steve_sign, sizeof steve_sign);
    fl_kq_encoded_free(&enc);
    return 0;
}

int main(void) {
    char tmpl[] = "/tmp/fl_kq_netXXXXXX";
    char path[PATH_MAX];
    int rc = 0;

    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
    if (!mkdtemp(tmpl))
        return 1;
    snprintf(s_dir, sizeof s_dir, "%s", tmpl);
    snprintf(path, sizeof path, "%s/users.db", s_dir);
    if (fl_user_db_load(&s_db, path) != FL_RESULT_OK ||
        fl_user_db_add_user_under(&s_db, "Root.Flinstone", "Mya", "pw-mya", 4000, 0, 0) != FL_RESULT_OK ||
        fl_user_db_add_user_under(&s_db, "Root.Flinstone", "Steve", "pw-steve", 4001, 0, 0) != FL_RESULT_OK)
        return 1;
    member(0, "Root", 1);
    member(1, "Mya", 0);
    member(2, "Steve", 0);
    snprintf(path, sizeof path, "%s/store", s_dir);
    if (fl_net_kq_install(the_users, path) != FL_RESULT_OK)
        return 1;
    fl_net_kq_set_transport(host_send, client_send);
    fl_net_kq_set_clock(fake_now);

#define RUN(t) do { \
        if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_keys_come_from_the_host_registry);
    RUN(test_co_signed_delivery_over_the_wire);
    RUN(test_an_unanswered_request_leaks_away);
    fl_net_kq_reset();
    rmrf(s_dir);
    if (rc == 0)
        printf("All KeyQuorum delivery wire tests passed.\n");
    return rc;
}
