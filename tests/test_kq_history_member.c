/**
 * test_kq_history_member.c - end to end in process: a session member downloads a co-signed delivery's
 * history from the host and opens it (fl_net_kq_request_history, then fl_net_kq_history_open).
 *
 * The host's gate and source in the native shell (kq_history_gate, kq_history_source) are static in
 * userland/command/cmd_server_file.c, which links the whole shell. So this test installs a gate and a
 * source that do what those do, over the real producer; the policy check the native gate adds
 * (fl_authz_subsystem_check) is not part of this test. The member side is the verb's own code
 * (userland/command/cmd_server_history.c).
 *
 * A recording transport stands in for sockets, as in test_kq_net.c: frames a member sends go to the host
 * handler as that member, and frames the host sends to a member go to the client handler.
 *
 * Passwords are drawn at run time; none is written here.
 */
#include "net_kq_delivery.h"
#include "cmd_server_history.h"
#include "kq_checkpoint.h"
#include "kq_delivery.h"
#include "kq_history_bundle.h"
#include "fl/mem_asm.h"
#include "fl/random.h"
#include "user_db.h"
#include "tests/kq_net_fake.h"
#include "tests/kq_test_util.h"

#include <dirent.h>
#include <pthread.h>
#include <limits.h>
#include "fl/kq.h"
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

/* For a test that owns resources: fail, then release them at out. */
#define CHECK(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        rc = 1; \
        goto out; \
    } \
} while (0)

/* KeyQuorum's KQER code for Denied: the word the host refuses a gate's "no" with (net_kq_delivery.c). */
#define KQER_DENIED 8

#define MYA "Root.Flinstone.Mya"
#define ANA "Root.Flinstone.Ana"
#define STEVE "Root.Flinstone.Steve"

/* ---- recording transport ---------------------------------------------------------------------------- */

#define MEMBER_ANA (&s_members[1])
#define MEMBER_STEVE (&s_members[2])

static int s_data_to[3];

static void host_send(fl_net_server_t *srv, const fl_net_server_member_t *to, uint8_t opcode,
                      const uint8_t *payload, uint16_t plen) {
    (void)srv;
    kq_net_fake_note_error(opcode, payload, plen);
    for (int i = 1; i < 3; i++)
        if (to == &s_members[i]) {
            if (opcode == FL_NET_SESSION_OP_KQH_DATA)
                s_data_to[i]++;
            fl_net_kq_client_handle(NULL, opcode, payload, plen);
        }
}

static void client_send(fl_net_client_t *client, uint8_t opcode, const uint8_t *payload, uint16_t plen) {
    (void)client;
    fl_net_kq_host_handle(s_srv, s_actor, opcode, payload, plen);
}

/* ---- fixtures ----------------------------------------------------------------------------------------- */

static char s_dir[256];
/* Drawn at run time (fl_random_bytes, hex); wiped at exit. */
static char s_pw_mya[33], s_pw_ana[33], s_pw_steve[33];

/* Mya checks a file in and seals it to Steve: the container beside the file and the delivery. */
static int encode_from_mya(const char *name, const void *data, size_t n, char path[PATH_MAX],
                           fl_kq_encoded_t *enc) {
    unsigned char sign[FL_USER_SIGNING_SECRET_LEN];
    int rc;

    snprintf(path, PATH_MAX, "%s/%s", s_dir, name);
    if (kq_test_write_file(path, data, n) != 0 || fl_user_db_open_key(&s_db, "Mya", s_pw_mya, sign) != FL_RESULT_OK)
        return -1;
    rc = fl_kq_cp_encode(&s_db, MYA, sign, STEVE, fl_user_db_box_public(&s_db, STEVE), path,
                         "2026-11-16T09:00:00.000Z", enc) == FL_KQ_CP_OK ? 0 : -1;
    asm_mem_secure_zero(sign, sizeof sign);
    return rc;
}

/* ---- the host side: what kq_history_gate and kq_history_source do --------------------------------------- */

/* The host shell's record of a delivery it sent (kq_sent_remember): who it went to, the file it sits beside. */
#define SENT_MAX 4u
typedef struct {
    int used;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    char recipient[64];
    char path[PATH_MAX];
} sent_t;
static sent_t s_sent[SENT_MAX];

static void sent_remember(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const char *recipient, const char *path) {
    for (unsigned i = 0; i < SENT_MAX; i++)
        if (!s_sent[i].used) {
            s_sent[i].used = 1;
            memcpy(s_sent[i].id, id, FL_KQ_DELIVERY_ID_LEN);
            snprintf(s_sent[i].recipient, sizeof s_sent[i].recipient, "%s", recipient);
            snprintf(s_sent[i].path, sizeof s_sent[i].path, "%s", path);
            return;
        }
}

static const sent_t *sent_find(const uint8_t id[FL_KQ_DELIVERY_ID_LEN]) {
    for (unsigned i = 0; i < SENT_MAX; i++)
        if (s_sent[i].used && memcmp(s_sent[i].id, id, FL_KQ_DELIVERY_ID_LEN) == 0)
            return &s_sent[i];
    return NULL;
}

/* How the source misbehaves, for the member-side checks. */
enum {
    SRC_REAL,           /* as kq_history_source */
    SRC_OTHER_SNAPSHOT, /* the snapshot of another file: valid letters, but it does not cover the history */
    SRC_FILE_TWICE,     /* the file letter in both slots: no snapshot */
    SRC_SNAPSHOT_TWICE, /* the snapshot in both slots: no file letter */
    SRC_OTHER_FILE      /* both letters of another file: they open and cover, but it is not this delivery's file */
};
static int s_source_mode = SRC_REAL;
static int s_host_key_available = 1; /* fl_kq_obtain_author_key succeeds */
static char s_other_path[PATH_MAX];
static int s_gate_calls, s_source_calls;

/* kq_history_gate: only the delivery's recipient. */
static int hist_gate(const char *requester, const uint8_t id[FL_KQ_DELIVERY_ID_LEN]) {
    const sent_t *e = sent_find(id);

    s_gate_calls++;
    return requester && e && strcmp(e->recipient, requester) == 0;
}

/* kq_history_source: the host account's key (ACCES without it), the real letters, packed. */
static fl_result_t hist_source(const char *requester, const uint8_t id[FL_KQ_DELIVERY_ID_LEN], uint8_t *out,
                               size_t cap, size_t *out_len) {
    const sent_t *e = sent_find(id);
    fl_user_db_keydir_t dir;
    fl_kq_buf_t l15 = {0}, l17 = {0}, o15 = {0}, o17 = {0};
    unsigned char key[FL_USER_SIGNING_SECRET_LEN];
    fl_kq_cp_result_t cp;
    fl_result_t rc = FL_RESULT_ERR;

    s_source_calls++;
    if (!e)
        return FL_RESULT_NOENT;
    if (!s_host_key_available)
        return FL_RESULT_ACCES;
    if (fl_user_db_open_key(&s_db, "Mya", s_pw_mya, key) != FL_RESULT_OK) /* stands in for the key service */
        return FL_RESULT_ACCES;
    fl_user_db_keydir_init(&dir, &s_db);
    cp = fl_kq_cp_history_letters_with(&dir.base, MYA, key, requester,
                                       s_source_mode == SRC_OTHER_FILE ? s_other_path : e->path, &l15, &l17);
    if (cp == FL_KQ_CP_OK && s_source_mode == SRC_OTHER_SNAPSHOT)
        cp = fl_kq_cp_history_letters_with(&dir.base, MYA, key, requester, s_other_path, &o15, &o17);
    asm_mem_secure_zero(key, sizeof key);
    if (cp != FL_KQ_CP_OK) {
        rc = fl_kq_cp_to_result(cp) == FL_RESULT_OK ? FL_RESULT_ERR : fl_kq_cp_to_result(cp);
        goto done;
    }
    switch (s_source_mode) {
    case SRC_OTHER_SNAPSHOT:
        rc = fl_history_bundle_pack(out, cap, out_len, l15.data, l15.len, o17.data, o17.len);
        break;
    case SRC_FILE_TWICE:
        rc = fl_history_bundle_pack(out, cap, out_len, l15.data, l15.len, l15.data, l15.len);
        break;
    case SRC_SNAPSHOT_TWICE:
        rc = fl_history_bundle_pack(out, cap, out_len, l17.data, l17.len, l17.data, l17.len);
        break;
    default:
        rc = fl_history_bundle_pack(out, cap, out_len, l15.data, l15.len, l17.data, l17.len);
        break;
    }
done:
    fl_kq_buf_free(&l15);
    fl_kq_buf_free(&l17);
    fl_kq_buf_free(&o15);
    fl_kq_buf_free(&o17);
    return rc;
}

/* ---- the member side ------------------------------------------------------------------------------------ */

static unsigned char s_steve_box[FL_USER_SIGNING_SECRET_LEN], s_ana_box[FL_USER_SIGNING_SECRET_LEN];
static fl_kq_encoded_t s_enc;           /* plan.txt, Mya to Steve */
static char s_plan_path[PATH_MAX];
static char s_sender[FL_HERITAGE_KEY_ID_MAX]; /* the sender Steve's shell saw on the delivery */

static int count_line(const char *line, size_t n, void *ctx) {
    (void)line;
    (void)n;
    (*(unsigned *)ctx)++;
    return 0;
}

static pthread_mutex_t s_session_mu = PTHREAD_MUTEX_INITIALIZER; /* stands in for ctx->mutex */

/* What `server file history <id>` does on the member after its password prompt (fl_server_history_fetch):
 * request under the session lock, open bound to the delivery's file id when known, drop on timeout. */
static fl_result_t member_history(const fl_net_server_member_t *who, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                                  const unsigned char box[32], const char *sender, const char *me,
                                  const uint8_t sender_public[32], unsigned wait_ms, fl_kq_buf_t *lines,
                                  uint32_t *events) {
    fl_server_history_got_t got;

    /* The decoded record when there is one (its file id binds the answer); the sender as the test names it. */
    if (!fl_server_history_find(id, &got)) {
        memset(&got, 0, sizeof got);
        memcpy(got.id, id, FL_KQ_DELIVERY_ID_LEN);
    }
    snprintf(got.sender, sizeof got.sender, "%s", sender);
    s_actor = who;
    return fl_server_history_fetch(&s_session_mu, s_srv, NULL, &got, box, me, sender_public, wait_ms, lines,
                                   events);
}

/* The same, with no decoded record (the -from path): nothing to bind the file to. */
static fl_result_t member_history_unbound(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], fl_kq_buf_t *lines,
                                          uint32_t *events) {
    fl_server_history_got_t got;

    memset(&got, 0, sizeof got);
    memcpy(got.id, id, FL_KQ_DELIVERY_ID_LEN);
    snprintf(got.sender, sizeof got.sender, "%s", MYA);
    s_actor = MEMBER_STEVE;
    return fl_server_history_fetch(&s_session_mu, s_srv, NULL, &got, s_steve_box, STEVE,
                                   fl_user_db_signing_public(&s_db, MYA), 0, lines, events);
}

/* ---- tests ------------------------------------------------------------------------------------------------ */

/* Mya sends plan.txt to Steve over the wire; Steve's shell sees it with Mya as its sender, and once decoded
 * remembers it as kq_decode_and_ack does (id, sender, file name, the offer's file id). */
static int test_a_delivery_mya_sends_to_steve(void) {
    static const char text[] = "quarterly plan v1\n";
    fl_net_kq_inbox_t inbox[4];
    fl_server_history_got_t got;

    ASSERT(encode_from_mya("plan.txt", text, sizeof text - 1, s_plan_path, &s_enc) == 0);
    s_actor = &s_members[0];
    ASSERT(fl_net_kq_send_delivery(s_srv, NULL, 0, MYA, STEVE, &s_enc) == FL_RESULT_OK);
    ASSERT(fl_net_kq_inbox(inbox, 4) == 1);
    ASSERT(memcmp(inbox[0].id, s_enc.delivery_id, FL_KQ_DELIVERY_ID_LEN) == 0);
    snprintf(s_sender, sizeof s_sender, "%s", inbox[0].sender);
    ASSERT(strcmp(s_sender, MYA) == 0);
    fl_server_history_remember(inbox[0].id, inbox[0].sender, inbox[0].file_name, inbox[0].offer.file_id);
    ASSERT(fl_server_history_find(s_enc.delivery_id, &got) == 1);
    ASSERT(got.has_file_id && memcmp(got.file_id, s_enc.offer.file_id, sizeof got.file_id) == 0);
    ASSERT(strcmp(got.sender, MYA) == 0 && strcmp(got.file_name, "plan.txt") == 0);
    fl_net_kq_inbox_drop(inbox[0].id);
    sent_remember(s_enc.delivery_id, STEVE, s_plan_path);
    return 0;
}

/* The real store: the host's gate and the real source answer, and the events open for Steve. */
static int test_the_recipient_downloads_and_opens_the_history(void) {
    fl_net_kq_keys_t mya;
    fl_kq_buf_t lines = {0};
    uint32_t events = 0;
    unsigned n = 0;
    int data_before = s_data_to[2];
    int rc = 0;

    s_source_mode = SRC_REAL;
    s_gate_calls = s_source_calls = 0;
    /* Steve's shell gets the sender's key from the host registry, by its exact id. */
    s_actor = MEMBER_STEVE;
    CHECK(fl_net_kq_lookup(s_srv, NULL, 0, s_sender, 0, &mya) == FL_RESULT_OK);
    CHECK(fl_server_history_sender_matches(&mya, s_sender));
    CHECK(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, s_sender, STEVE, mya.signing_public, 0,
                         &lines, &events) == FL_RESULT_OK);
    CHECK(s_gate_calls == 1 && s_source_calls == 1);
    CHECK(s_data_to[2] > data_before);
    CHECK(fl_kq_cp_for_each_event(&lines, count_line, &n) == n && n >= 1);
    CHECK(events >= n); /* the snapshot is the whole history, the delivered file's a prefix of it */
out:
    fl_kq_buf_free(&lines);
    return rc;
}

/* The host account's key is looked up by its exact id: a short name resolves, but is not the sender. */
static int test_the_sender_key_must_be_the_exact_account(void) {
    fl_net_kq_keys_t k;

    s_actor = MEMBER_STEVE;
    ASSERT(fl_net_kq_lookup(s_srv, NULL, 0, "Mya", 0, &k) == FL_RESULT_OK && k.found);
    ASSERT(strcmp(k.key_id, MYA) == 0);
    ASSERT(!fl_server_history_sender_matches(&k, "Mya"));  /* the name a -from might give */
    ASSERT(fl_server_history_sender_matches(&k, MYA));
    ASSERT(!fl_server_history_sender_matches(&k, ANA));
    ASSERT(!fl_server_history_sender_matches(&k, ""));
    k.found = 0;
    ASSERT(!fl_server_history_sender_matches(&k, MYA));     /* a registry miss never matches */
    ASSERT(!fl_server_history_sender_matches(NULL, MYA));
    return 0;
}

/* The host's key is not available: the source says ACCES, the host refuses, nothing arrives. */
static int test_without_the_host_key_the_request_is_refused_and_times_out(void) {
    fl_kq_buf_t lines = {0};
    uint32_t events = 0;
    int data_before = s_data_to[2];

    s_host_key_available = 0;
    s_last_err[0] = '\0';
    ASSERT(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                          fl_user_db_signing_public(&s_db, MYA), 40, &lines, &events) == FL_RESULT_TIMEDOUT);
    s_host_key_available = 1;
    ASSERT(strstr(s_last_err, "history refused: ") && strstr(s_last_err, "no history for that delivery"));
    ASSERT(s_data_to[2] == data_before);
    ASSERT(lines.data == NULL && lines.len == 0 && events == 0);
    return 0;
}

/* A request the member gives up on stays pending until it is dropped (the fetch drops it); then it is gone. */
static int test_a_timed_out_request_waits_until_dropped(void) {
    uint8_t req[16], a[16], b[16];
    fl_kq_buf_t l15 = {0}, l17 = {0}, lines = {0};
    uint32_t events = 0;

    s_host_key_available = 0;
    s_actor = MEMBER_STEVE;
    ASSERT(fl_net_kq_request_history(s_srv, NULL, 0, s_enc.delivery_id, req) == FL_RESULT_OK);
    s_host_key_available = 1;
    ASSERT(fl_net_kq_history_open(req, 0, s_steve_box, MYA, STEVE, fl_user_db_signing_public(&s_db, MYA), NULL,
                                  &lines, &events) == FL_RESULT_TIMEDOUT);
    ASSERT(fl_net_kq_history_take(req, 0, &l15, &l17) == FL_RESULT_TIMEDOUT); /* still pending */
    fl_net_kq_history_drop(req);
    ASSERT(fl_net_kq_history_take(req, 0, &l15, &l17) == FL_RESULT_NOENT);
    /* Bad arguments drop the request too, as documented. */
    ASSERT(fl_net_kq_request_history(s_srv, NULL, 0, s_enc.delivery_id, req) == FL_RESULT_OK);
    ASSERT(fl_net_kq_history_open(req, 0, NULL, MYA, STEVE, fl_user_db_signing_public(&s_db, MYA), NULL, &lines,
                                  &events) == FL_RESULT_INVAL);
    ASSERT(fl_net_kq_history_take(req, 0, &l15, &l17) == FL_RESULT_NOENT);
    /* Timed-out fetches leave no slot behind: both slots are still free afterwards. */
    s_host_key_available = 0;
    for (int i = 0; i < 3; i++)
        ASSERT(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                              fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_TIMEDOUT);
    s_host_key_available = 1;
    ASSERT(fl_net_kq_request_history(s_srv, NULL, 0, s_enc.delivery_id, a) == FL_RESULT_OK);
    fl_net_kq_history_drop(a);
    s_host_key_available = 0;
    ASSERT(fl_net_kq_request_history(s_srv, NULL, 0, s_enc.delivery_id, a) == FL_RESULT_OK);
    ASSERT(fl_net_kq_request_history(s_srv, NULL, 0, s_enc.delivery_id, b) == FL_RESULT_OK);
    s_host_key_available = 1;
    fl_net_kq_history_drop(a);
    fl_net_kq_history_drop(b);
    return 0;
}

/* Ana is not the recipient: the gate refuses, the source never runs, nothing arrives. */
static int test_a_requester_who_is_not_the_recipient_is_refused(void) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    fl_kq_buf_t lines = {0};
    uint32_t events = 0;
    uint8_t unknown[FL_KQ_DELIVERY_ID_LEN];

    s_gate_calls = s_source_calls = 0;
    s_last_err[0] = '\0';
    ASSERT(member_history(MEMBER_ANA, s_enc.delivery_id, s_ana_box, MYA, ANA, fl_user_db_signing_public(&s_db, MYA),
                          0, &lines, &events) == FL_RESULT_TIMEDOUT);
    ASSERT(s_gate_calls == 1 && s_source_calls == 0);
    ASSERT(strstr(s_last_err, "history refused: ") && strstr(s_last_err, drv->error_name(KQER_DENIED)));
    ASSERT(s_data_to[1] == 0);
    /* An id the host never sent is refused the same way. */
    memset(unknown, 0x5a, sizeof unknown);
    ASSERT(member_history(MEMBER_STEVE, unknown, s_steve_box, MYA, STEVE, fl_user_db_signing_public(&s_db, MYA), 0,
                          &lines, &events) == FL_RESULT_TIMEDOUT);
    ASSERT(s_source_calls == 0);
    return 0;
}

/* A history bigger than FL_HISTORY_BUNDLE_MAX_BYTES is refused whole, never truncated. */
static int test_an_oversized_history_is_refused_not_truncated(void) {
    const size_t big = FL_HISTORY_BUNDLE_MAX_BYTES + FL_HISTORY_BUNDLE_MAX_BYTES / 8u;
    uint8_t *data = (uint8_t *)malloc(big);
    uint8_t *bundle = (uint8_t *)malloc(FL_HISTORY_BUNDLE_MAX_BYTES);
    unsigned char key[FL_USER_SIGNING_SECRET_LEN];
    fl_user_db_keydir_t dir;
    fl_kq_encoded_t enc;
    fl_kq_buf_t l15 = {0}, l17 = {0}, lines = {0};
    char path[PATH_MAX];
    size_t blen = 0;
    uint32_t events = 0;
    int data_before = s_data_to[2];
    int have_enc = 0, rc = 0;

    memset(&enc, 0, sizeof enc);
    CHECK(data && bundle);
    CHECK(fl_random_bytes(data, big) == 0); /* does not compress */
    CHECK(encode_from_mya("big.bin", data, big, path, &enc) == 0);
    have_enc = 1;
    sent_remember(enc.delivery_id, STEVE, path);

    /* The real letters are over the bound, and packing them fails rather than cutting them short. */
    CHECK(fl_user_db_open_key(&s_db, "Mya", s_pw_mya, key) == FL_RESULT_OK);
    fl_user_db_keydir_init(&dir, &s_db);
    CHECK(fl_kq_cp_history_letters_with(&dir.base, MYA, key, STEVE, path, &l15, &l17) == FL_KQ_CP_OK);
    CHECK(l15.len + l17.len > FL_HISTORY_BUNDLE_MAX_BYTES);
    CHECK(fl_history_bundle_pack(bundle, FL_HISTORY_BUNDLE_MAX_BYTES, &blen, l15.data, l15.len, l17.data,
                                 l17.len) == FL_RESULT_INVAL);
    CHECK(blen == 0);

    /* Over the wire: refused, and not one DATA frame goes out. */
    s_last_err[0] = '\0';
    CHECK(member_history(MEMBER_STEVE, enc.delivery_id, s_steve_box, MYA, STEVE,
                         fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_TIMEDOUT);
    CHECK(strstr(s_last_err, "no history for that delivery"));
    CHECK(s_data_to[2] == data_before);
out:
    asm_mem_secure_zero(key, sizeof key);
    fl_kq_buf_free(&l15);
    fl_kq_buf_free(&l17);
    fl_kq_buf_free(&lines);
    if (have_enc)
        fl_kq_encoded_free(&enc);
    free(data);
    free(bundle);
    return rc;
}

/* The letters must be signed by the expected sender under its key, and addressed to the one who opens them. */
static int test_a_wrong_sender_or_key_does_not_open(void) {
    fl_kq_buf_t lines = {0};
    uint32_t events = 0;

    s_source_mode = SRC_REAL;
    /* A sender the letters are not from. */
    ASSERT(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, ANA, STEVE,
                          fl_user_db_signing_public(&s_db, ANA), 0, &lines, &events) == FL_RESULT_ACCES);
    ASSERT(lines.data == NULL && events == 0);
    /* The right sender's name with someone else's key. */
    ASSERT(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                          fl_user_db_signing_public(&s_db, ANA), 0, &lines, &events) == FL_RESULT_ACCES);
    /* The right sender's key under another name. */
    ASSERT(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, ANA, STEVE,
                          fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_ACCES);
    /* Letters sealed to Steve do not open with another box key. */
    ASSERT(member_history(MEMBER_STEVE, s_enc.delivery_id, s_ana_box, MYA, STEVE,
                          fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_ACCES);
    ASSERT(lines.data == NULL && events == 0);
    return 0;
}

/* other.txt: a second file Mya sealed to Steve, for the answers about the wrong file. */
static int ensure_other_file(void) {
    static const char text[] = "an unrelated file\nwith its own history\n";
    fl_kq_encoded_t other;

    if (s_other_path[0])
        return 0;
    if (encode_from_mya("other.txt", text, sizeof text - 1, s_other_path, &other) != 0)
        return -1;
    fl_kq_encoded_free(&other);
    return 0;
}

/* Valid letters whose snapshot is another file's: the snapshot does not cover the history. */
static int test_a_snapshot_that_does_not_cover_the_history_is_refused(void) {
    fl_kq_buf_t lines = {0};
    uint32_t events = 0;

    ASSERT(ensure_other_file() == 0);
    s_source_mode = SRC_OTHER_SNAPSHOT;
    ASSERT(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                          fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_ERR);
    s_source_mode = SRC_REAL;
    ASSERT(lines.data == NULL && lines.len == 0 && events == 0);
    return 0;
}

/* The host answers plan.txt's delivery with both letters of other.txt. They open and the snapshot covers
 * them, but they are not the delivery's file: refused when the member knows the file id (it decoded the
 * delivery), and only the unbound -from path, which says so in the verb, takes them. */
static int test_both_letters_of_another_file_are_refused(void) {
    fl_kq_buf_t lines = {0};
    uint32_t events = 0;
    int rc = 0;

    CHECK(ensure_other_file() == 0);
    s_source_mode = SRC_OTHER_FILE;
    CHECK(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                         fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_NET_KQ_HISTORY_OTHER_FILE);
    CHECK(lines.data == NULL && lines.len == 0 && events == 0);
    CHECK(member_history_unbound(s_enc.delivery_id, &lines, &events) == FL_RESULT_OK);
    fl_kq_buf_free(&lines);
    events = 0;
    /* The right file still opens with its file id. */
    s_source_mode = SRC_REAL;
    CHECK(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                         fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_OK);
out:
    s_source_mode = SRC_REAL;
    fl_kq_buf_free(&lines);
    return rc;
}

/* The file id is the file's, not the revision's: after Mya checks in a newer revision, the host's fresh reseal
 * of the new head still matches the id Steve recorded at decode. */
static int test_the_file_id_still_matches_after_the_head_moves(void) {
    static const char text[] = "quarterly plan v2\n";
    fl_kq_encoded_t newer;
    fl_kq_buf_t lines = {0};
    uint32_t events = 0;
    unsigned n = 0;
    char path[PATH_MAX];
    int rc = 0, have = 0;

    memset(&newer, 0, sizeof newer);
    CHECK(encode_from_mya("plan.txt", text, sizeof text - 1, path, &newer) == 0);
    have = 1;
    CHECK(strcmp(path, s_plan_path) == 0);
    CHECK(memcmp(newer.offer.file_id, s_enc.offer.file_id, sizeof newer.offer.file_id) == 0);
    CHECK(memcmp(newer.offer.revision_id, s_enc.offer.revision_id, sizeof newer.offer.revision_id) != 0);
    CHECK(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                         fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_OK);
    CHECK(fl_kq_cp_for_each_event(&lines, count_line, &n) == n && n >= 2); /* both check-ins */
out:
    fl_kq_buf_free(&lines);
    if (have)
        fl_kq_encoded_free(&newer);
    return rc;
}

/* Both letters are required: a bundle without the snapshot, or without the file letter, does not open. */
static int test_both_letters_are_required(void) {
    fl_kq_buf_t lines = {0};
    uint32_t events = 0;
    uint8_t bundle[64];
    size_t blen = 0;
    const uint8_t one = 1;
    int rc = 0;

    s_source_mode = SRC_FILE_TWICE;
    CHECK(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                         fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_ACCES);
    s_source_mode = SRC_SNAPSHOT_TWICE;
    CHECK(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                         fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_ACCES);
    s_source_mode = SRC_REAL;
    CHECK(lines.data == NULL && events == 0);
    /* A host cannot even build a bundle with one letter missing. */
    CHECK(fl_history_bundle_pack(bundle, sizeof bundle, &blen, &one, 1, &one, 0) == FL_RESULT_INVAL);
    CHECK(fl_history_bundle_pack(bundle, sizeof bundle, &blen, &one, 0, &one, 1) == FL_RESULT_INVAL);
    /* And after all that, the real answer still opens. */
    CHECK(member_history(MEMBER_STEVE, s_enc.delivery_id, s_steve_box, MYA, STEVE,
                         fl_user_db_signing_public(&s_db, MYA), 0, &lines, &events) == FL_RESULT_OK);
out:
    s_source_mode = SRC_REAL;
    fl_kq_buf_free(&lines);
    return rc;
}

/* ---- the verb's argument and sender resolution (fl_server_history_args) ---------------------------------- */

static fl_server_history_args_t args_of(char **argv, int argc, fl_server_history_got_t *got, const char **bad) {
    const char *hex = NULL;

    return fl_server_history_args(argc, argv, got, &hex, bad);
}

static int test_the_history_arguments_resolve_the_sender(void) {
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1], unknown_hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    uint8_t unknown[FL_KQ_DELIVERY_ID_LEN];
    fl_server_history_got_t got;
    const char *bad = NULL;
    char long_key[FL_HERITAGE_KEY_ID_MAX + 8];

    fl_kq_delivery_id_hex(s_enc.delivery_id, hex);
    memset(unknown, 0x3c, sizeof unknown);
    fl_kq_delivery_id_hex(unknown, unknown_hex);
    memset(long_key, 'k', sizeof long_key - 1);
    long_key[sizeof long_key - 1] = '\0';
    {
        /* Decoded here: the remembered sender and file id, with or without a -from that agrees. */
        char *a1[] = {"history", hex};
        char *a2[] = {"history", hex, "-from", MYA};
        char *a3[] = {"history", "-from", MYA, hex};
        ASSERT(args_of(a1, 2, &got, &bad) == FL_SERVER_HISTORY_ARGS_OK);
        ASSERT(strcmp(got.sender, MYA) == 0 && got.has_file_id && strcmp(got.file_name, "plan.txt") == 0);
        ASSERT(memcmp(got.file_id, s_enc.offer.file_id, sizeof got.file_id) == 0);
        ASSERT(args_of(a2, 4, &got, &bad) == FL_SERVER_HISTORY_ARGS_OK);
        ASSERT(args_of(a3, 4, &got, &bad) == FL_SERVER_HISTORY_ARGS_OK && got.has_file_id);
    }
    {
        /* A -from that disagrees with the delivery's sender is refused. */
        char *a[] = {"history", hex, "-from", ANA};
        ASSERT(args_of(a, 4, &got, &bad) == FL_SERVER_HISTORY_ARGS_FROM_MISMATCH);
    }
    {
        /* Not decoded here: -from is required, and then nothing binds the file. */
        char *a1[] = {"history", unknown_hex};
        char *a2[] = {"history", unknown_hex, "-from", MYA};
        ASSERT(args_of(a1, 2, &got, &bad) == FL_SERVER_HISTORY_ARGS_NO_SENDER);
        ASSERT(args_of(a2, 4, &got, &bad) == FL_SERVER_HISTORY_ARGS_OK);
        ASSERT(strcmp(got.sender, MYA) == 0 && !got.has_file_id && memcmp(got.id, unknown, sizeof unknown) == 0);
    }
    {
        /* An empty or oversized -from, a share id, and malformed command lines. */
        char *empty[] = {"history", unknown_hex, "-from", ""};
        char *toolong[] = {"history", unknown_hex, "-from", long_key};
        char *share[] = {"history", "share-42"};
        char *none[] = {"history"};
        char *dangling[] = {"history", hex, "-from"};
        char *twice[] = {"history", hex, "-from", MYA, "-from", MYA};
        char *two_ids[] = {"history", hex, unknown_hex};
        ASSERT(args_of(empty, 4, &got, &bad) == FL_SERVER_HISTORY_ARGS_BAD_FROM);
        ASSERT(args_of(toolong, 4, &got, &bad) == FL_SERVER_HISTORY_ARGS_BAD_FROM);
        ASSERT(args_of(share, 2, &got, &bad) == FL_SERVER_HISTORY_ARGS_NOT_DELIVERY);
        ASSERT(args_of(none, 1, &got, &bad) == FL_SERVER_HISTORY_ARGS_USAGE && bad == NULL);
        ASSERT(args_of(dangling, 3, &got, &bad) == FL_SERVER_HISTORY_ARGS_USAGE && bad && !strcmp(bad, "-from"));
        ASSERT(args_of(twice, 6, &got, &bad) == FL_SERVER_HISTORY_ARGS_USAGE);
        ASSERT(args_of(two_ids, 3, &got, &bad) == FL_SERVER_HISTORY_ARGS_USAGE && bad == unknown_hex);
    }
    return 0;
}

/* The ring: the newest 16, the same id replacing its own record, bad input never stored. */
static int test_the_decoded_deliveries_ring(void) {
    fl_server_history_got_t got;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN], fid[16];
    char long_name[200];

    memset(fid, 0x11, sizeof fid);
    memset(long_name, 'n', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    memset(id, 0, sizeof id);
    id[0] = 0xee;
    fl_server_history_remember(id, MYA, long_name, fid);   /* name too long */
    fl_server_history_remember(id, "", "a.txt", fid);       /* no sender */
    fl_server_history_remember(id, MYA, "a.txt", NULL);     /* no file id */
    ASSERT(fl_server_history_find(id, &got) == 0);
    fl_server_history_remember(id, MYA, "a.txt", fid);
    fid[0] = 0x22;
    fl_server_history_remember(id, MYA, "a2.txt", fid);     /* the same delivery: replaced in place */
    ASSERT(fl_server_history_find(id, &got) == 1 && strcmp(got.file_name, "a2.txt") == 0 && got.file_id[0] == 0x22);
    /* 15 more distinct deliveries: 17 remembered in all (plan.txt, 0xee, 15), so the oldest, plan.txt, is gone. */
    for (unsigned i = 0; i < FL_SERVER_HISTORY_GOT_MAX - 1u; i++) {
        uint8_t other[FL_KQ_DELIVERY_ID_LEN] = {0};

        other[0] = 0xd0;
        other[1] = (uint8_t)i;
        fl_server_history_remember(other, MYA, "b.txt", fid);
    }
    ASSERT(fl_server_history_find(id, &got) == 1);
    ASSERT(fl_server_history_find(s_enc.delivery_id, &got) == 0);
    fl_server_history_forget_all();
    ASSERT(fl_server_history_find(id, &got) == 0);
    ASSERT(fl_server_history_find(NULL, &got) == 0);
    return 0;
}

int main(void) {
    char tmpl[] = "/tmp/fl_kq_hist_memberXXXXXX";
    char path[PATH_MAX];
    int rc = 0;

    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
    if (!fl_kq_driver_get_or_register()) {
        fprintf(stderr, "KeyQuorum driver not available\n");
        return 1;
    }
    if (!mkdtemp(tmpl))
        return 1;
    snprintf(s_dir, sizeof s_dir, "%s", tmpl);
    snprintf(path, sizeof path, "%s/users.db", s_dir);
    if (kq_test_random_hex(s_pw_mya, 16) != 0 || kq_test_random_hex(s_pw_ana, 16) != 0 || kq_test_random_hex(s_pw_steve, 16) != 0 ||
        fl_user_db_load(&s_db, path) != FL_RESULT_OK ||
        fl_user_db_add_user_under(&s_db, "Root.Flinstone", "Mya", s_pw_mya, 4000, 0, 0) != FL_RESULT_OK ||
        fl_user_db_add_user_under(&s_db, "Root.Flinstone", "Ana", s_pw_ana, 4001, 0, 0) != FL_RESULT_OK ||
        fl_user_db_add_user_under(&s_db, "Root.Flinstone", "Steve", s_pw_steve, 4002, 0, 0) != FL_RESULT_OK ||
        fl_user_db_open_box(&s_db, "Steve", s_pw_steve, s_steve_box) != FL_RESULT_OK ||
        fl_user_db_open_box(&s_db, "Ana", s_pw_ana, s_ana_box) != FL_RESULT_OK) {
        rc = 1;
        goto out;
    }
    member(0, "Mya", 1); /* the host shell, signed in as Mya: the sender */
    member(1, "Ana", 0);
    member(2, "Steve", 0);
    snprintf(path, sizeof path, "%s/store", s_dir);
    if (fl_net_kq_install(the_users, path) != FL_RESULT_OK) {
        rc = 1;
        goto out;
    }
    fl_net_kq_set_transport(host_send, client_send);
    fl_net_kq_set_clock(fake_now);
    fl_net_kq_set_history(hist_gate, hist_source);

#define RUN(t) do { \
        if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_a_delivery_mya_sends_to_steve);
    RUN(test_the_recipient_downloads_and_opens_the_history);
    RUN(test_the_sender_key_must_be_the_exact_account);
    RUN(test_without_the_host_key_the_request_is_refused_and_times_out);
    RUN(test_a_timed_out_request_waits_until_dropped);
    RUN(test_a_requester_who_is_not_the_recipient_is_refused);
    RUN(test_an_oversized_history_is_refused_not_truncated);
    RUN(test_a_wrong_sender_or_key_does_not_open);
    RUN(test_a_snapshot_that_does_not_cover_the_history_is_refused);
    RUN(test_both_letters_of_another_file_are_refused);
    RUN(test_both_letters_are_required);
    RUN(test_the_history_arguments_resolve_the_sender);
    RUN(test_the_file_id_still_matches_after_the_head_moves);
    RUN(test_the_decoded_deliveries_ring); /* last: it rolls plan.txt out of the ring */
    fl_net_kq_set_history(NULL, NULL);
    fl_net_kq_reset();
out:
    fl_kq_encoded_free(&s_enc);
    asm_mem_secure_zero(s_steve_box, sizeof s_steve_box);
    asm_mem_secure_zero(s_ana_box, sizeof s_ana_box);
    asm_mem_secure_zero(s_pw_mya, sizeof s_pw_mya);
    asm_mem_secure_zero(s_pw_ana, sizeof s_pw_ana);
    asm_mem_secure_zero(s_pw_steve, sizeof s_pw_steve);
    kq_test_rmrf(s_dir);
    if (rc == 0)
        printf("All FL-04 member history tests passed.\n");
    return rc;
}
