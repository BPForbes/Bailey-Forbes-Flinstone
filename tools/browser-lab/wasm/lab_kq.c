/*
 * KeyQuorum co-signed delivery in the WebAssembly lab shell.
 *
 * The lab runs the native code unchanged: the checkpoints (kq_checkpoint.c), the host and
 * client of the delivery protocol (net_kq_delivery.c), the host's delivery store
 * (kq_delivery.c, SQLite from the KeyQuorum archive) and the driver (kq_driver.c). This
 * file supplies only what the browser provides in place of the native shell:
 *
 *   - C stand-ins for the memory primitives the assembly layer provides natively (there is
 *     no assembly in WebAssembly), keeping their shape: secure zero is zero plus a barrier;
 *   - the key directory (kq_keydir.h): each lab account's keys, made on first use and held
 *     in memory like the lab's passwords, and the keys other members publish to the host,
 *     where the first keys seen for a name are kept and different ones refused;
 *   - the members table, filled from the relay roster, and a transport that hands frames
 *     to the page (Module.onKqFrame) instead of sockets;
 *   - the shell verbs, written as steps that continue when an answer arrives: the browser
 *     gives the shell one thread, so nothing here waits.
 *
 * Files live in Emscripten's in-memory file system under /kq (containers, acceptance
 * records, the host's store); a delivered file is also written to the lab's ramfs, so `cat`
 * shows it, never over a ramfs file that is not this delivery's earlier copy.
 */
#include "lab_kq.h"

#include "identity.h"
#include "ramfs.h"

#include "fl/driver/keyquorum.h"
#include "fl/path.h"
#include "keyquorum_identity.h"
#include "kq_checkpoint.h"
#include "kq_delivery.h"
#include "kq_keydir.h"
#include "net_kq_delivery.h"
#include "server_shared_fs.h"
#include "common.h"

#include <emscripten.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* server_shared_fs.c resolves relative landing paths against g_cwd; the lab has no shell
 * common.c, so its working directory is defined here (the native shell sets its own). */
char g_cwd[CWD_MAX] = ".";

#define LAB_KEYS_MAX 32
#define LAB_LABEL_MAX 32
#define LAB_PENDING_MAX 8
#define LAB_INBOX_MAX 16
#define LAB_SEND_WAIT_SEC 30
#define LAB_KQ_ROOT "/kq"
#define LAB_HOST_DIR LAB_KQ_ROOT "/host"

/* ---- platform: what the assembly layer provides natively ------------------------------ */

void mem_domain_zero(void *ptr, size_t n) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;

    while (n--)
        *p++ = 0;
}

/* Extends mem_domain_zero, as natively: the same zeroing, then a barrier. */
void mem_domain_secure_zero(void *ptr, size_t n) {
    mem_domain_zero(ptr, n);
    __asm__ __volatile__("" ::: "memory");
}

/* Time depends only on n: no early exit. */
int asm_mem_ct_equal(const void *a, const void *b, size_t n) {
    const volatile unsigned char *x = (const volatile unsigned char *)a;
    const volatile unsigned char *y = (const volatile unsigned char *)b;
    unsigned char acc = 0;

    for (size_t i = 0; i < n; i++)
        acc |= (unsigned char)(x[i] ^ y[i]);
    return acc == 0;
}

/* The host's clock is this browser's clock. */
fl_result_t fl_time_wall_sec(int64_t *out_sec) {
    if (!out_sec)
        return FL_RESULT_INVAL;
    *out_sec = (int64_t)time(NULL);
    return FL_RESULT_OK;
}

/* ---- output ------------------------------------------------------------------------- */

static void (*s_emit)(const char *line);

/* Checkpoint outcomes and requests for the server panel. */
EM_JS(void, lab_kq_event_js, (const char *kind, const char *a, const char *b, const char *c), {
    if (typeof Module.onKqEvent === "function")
        Module.onKqEvent({ kind: UTF8ToString(kind), a: UTF8ToString(a), b: UTF8ToString(b),
                           c: UTF8ToString(c) });
});

static void say(const char *fmt, const char *a, const char *b, const char *c) {
    char line[384];

    snprintf(line, sizeof line, fmt, a ? a : "", b ? b : "", c ? c : "");
    if (s_emit) {
        s_emit(line);
        s_emit("\r\n");
    }
}

/* One checkpoint result: printed in the shell and listed in the panel's checkpoint log. */
static void checkpoint(const char *which, const char *outcome, const char *detail) {
    static const char prefix[] = "keyquorum: "; /* the host's refusals carry it already */

    if (detail && strncmp(detail, prefix, sizeof prefix - 1) == 0)
        detail += sizeof prefix - 1;
    say("keyquorum: %s checkpoint %s", which, outcome, NULL);
    if (detail && detail[0])
        say("  %s", detail, NULL, NULL);
    lab_kq_event_js("checkpoint", which, outcome, detail ? detail : "");
}

/* ---- key directory -------------------------------------------------------------------- */

typedef struct {
    int     used;
    int     local; /* an account of this lab shell: its secrets are held here */
    char    label[LAB_LABEL_MAX];
    uint8_t sign_pub[32];
    uint8_t box_pub[32];
    uint8_t sign_sec[32];
    uint8_t box_sec[32];
} lab_key_t;

static lab_key_t s_keys[LAB_KEYS_MAX];

/* Names match regardless of ASCII case, as the lab's accounts do ("root" finds Root); the
 * label kept is the spelling first seen, and that is what signatures name. */
static lab_key_t *key_find(const char *label) {
    for (int i = 0; i < LAB_KEYS_MAX; i++)
        if (s_keys[i].used && strcasecmp(s_keys[i].label, label) == 0)
            return &s_keys[i];
    return NULL;
}

static lab_key_t *key_slot(void) {
    for (int i = 0; i < LAB_KEYS_MAX; i++)
        if (!s_keys[i].used)
            return &s_keys[i];
    return NULL;
}

/* This lab account's keys, made on first use. */
static lab_key_t *local_keys(const char *label) {
    lab_key_t *k = key_find(label);

    if (k)
        return k->local ? k : NULL; /* a name already published by another member */
    if (!label || !label[0] || strlen(label) >= LAB_LABEL_MAX || !(k = key_slot()))
        return NULL;
    memset(k, 0, sizeof *k);
    if (kq_account_keygen(k->sign_sec, k->sign_pub) != KQ_ACCOUNT_OK ||
        kq_account_box_keygen(k->box_sec, k->box_pub) != KQ_ACCOUNT_OK) {
        mem_domain_secure_zero(k, sizeof *k);
        return NULL;
    }
    snprintf(k->label, sizeof k->label, "%s", label);
    k->local = 1;
    k->used = 1;
    return k;
}

static const char *dir_resolve(const fl_kq_keydir_t *dir, const char *who) {
    const lab_key_t *k;

    (void)dir;
    k = who ? key_find(who) : NULL;
    return k ? k->label : NULL;
}

static const unsigned char *dir_signing(const fl_kq_keydir_t *dir, const char *label) {
    const lab_key_t *k;

    (void)dir;
    k = label ? key_find(label) : NULL;
    return k ? k->sign_pub : NULL;
}

static const unsigned char *dir_box(const fl_kq_keydir_t *dir, const char *label) {
    const lab_key_t *k;

    (void)dir;
    k = label ? key_find(label) : NULL;
    return k ? k->box_pub : NULL;
}

static const fl_kq_keydir_t s_dir = {dir_resolve, dir_signing, dir_box};

static const fl_kq_keydir_t *lab_directory(void) {
    return &s_dir;
}

/* ---- members and transport -------------------------------------------------------------- */

static fl_net_server_member_t s_members[FL_NET_SERVER_MAX_MEMBERS];
static size_t s_member_count;
static fl_net_server_member_id_t s_self_id;
static int s_hosting;
/* The module treats the server as opaque; the lab's is just its members table. */
static fl_net_server_t *const s_srv = (fl_net_server_t *)&s_members;

size_t fl_net_server_member_count(const fl_net_server_t *srv) {
    (void)srv;
    return s_member_count;
}

const fl_net_server_member_t *fl_net_server_member_at(const fl_net_server_t *srv, size_t i) {
    (void)srv;
    return i < s_member_count ? &s_members[i] : NULL;
}

/* Never reached: the lab's transport (below) replaces sockets. */
fl_result_t fl_net_session_send_frame(fl_net_sock_handle_t h, uint8_t op, const uint8_t *p,
                                      uint16_t n) {
    (void)h; (void)op; (void)p; (void)n;
    return FL_RESULT_NOSYS;
}

EM_JS(void, lab_kq_post, (int to_host, int member_id, int op, const uint8_t *p, int n), {
    if (typeof Module.onKqFrame === "function")
        Module.onKqFrame({ toHost: !!to_host, memberId: member_id, op: op,
                           bytes: HEAPU8.slice(p, p + n) });
});

static const fl_net_server_member_t *self_member(void) {
    for (size_t i = 0; i < s_member_count; i++)
        if (s_members[i].member_id == s_self_id)
            return &s_members[i];
    return NULL;
}

/* Host to a member: its own client side directly, anyone else through the relay. */
static void host_send(fl_net_server_t *srv, const fl_net_server_member_t *to, uint8_t opcode,
                      const uint8_t *payload, uint16_t plen) {
    (void)srv;
    if (!to)
        return;
    if (to->member_id == s_self_id) {
        if (opcode == FL_NET_SESSION_OP_ERR) {
            char text[256];
            size_t n = plen < sizeof text - 1 ? plen : sizeof text - 1;

            memcpy(text, payload, n);
            text[n] = '\0';
            checkpoint("host", "refused", text);
            return;
        }
        fl_net_kq_client_handle(NULL, opcode, payload, plen);
        return;
    }
    lab_kq_post(0, to->member_id, opcode, payload, plen);
}

/* A client to the host: the host's own shell directly, otherwise through the relay. */
static void client_send(fl_net_client_t *client, uint8_t opcode, const uint8_t *payload,
                        uint16_t plen) {
    const fl_net_server_member_t *self = self_member();

    (void)client;
    if (s_hosting && self) {
        fl_net_kq_host_handle(s_srv, self, opcode, payload, plen);
        return;
    }
    lab_kq_post(1, 0, opcode, payload, plen);
}

/* ---- pending steps ---------------------------------------------------------------------- */

typedef struct {
    int     used;
    int     session;
    char    me[LAB_LABEL_MAX];
    char    to[LAB_LABEL_MAX];
    char    name[FL_FS_RAMFS_PATH];
    char    path[256];
    int64_t asked_at;
    int     permitted; /* --permitted: seal the head as it is, checking nothing in */
} pending_send_t;

typedef struct {
    int     used;
    int     session;
    char    me[LAB_LABEL_MAX];
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
} pending_open_t;

static pending_send_t s_sends[LAB_PENDING_MAX];
static pending_open_t s_opens[LAB_PENDING_MAX];
static uint8_t s_announced[LAB_INBOX_MAX][FL_KQ_DELIVERY_ID_LEN];
static size_t s_announced_n;

static int64_t now_sec(void) {
    int64_t t = 0;
    (void)fl_time_wall_sec(&t);
    return t;
}

static void utc_now(char out[32]) {
    time_t t = time(NULL);
    struct tm tm;

    gmtime_r(&t, &tm);
    strftime(out, 32, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
}

static int find_request(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], fl_net_kq_inbox_t *out) {
    fl_net_kq_inbox_t all[LAB_INBOX_MAX];
    size_t n = fl_net_kq_inbox(all, LAB_INBOX_MAX);

    for (size_t i = 0; i < n; i++) {
        if (memcmp(all[i].id, id, FL_KQ_DELIVERY_ID_LEN) == 0) {
            *out = all[i];
            return 0;
        }
    }
    return -1;
}

static void write_ramfs_copy(int session, const char *name, const fl_kq_buf_t *previous,
                             const char *dest) {
    char content[FL_FS_RAMFS_DATA];
    char existing[FL_FS_RAMFS_DATA];
    FILE *f = fopen(dest, "rb");
    size_t n = 0;
    int rc;

    if (f) {
        n = fread(content, 1, sizeof content, f);
        fclose(f);
    }
    if (!f || n >= sizeof content || memchr(content, 0, n)) {
        say("keyquorum: %s is kept in the KeyQuorum store only (lab files are text of at most "
            "159 bytes)", name, NULL, NULL);
        return;
    }
    content[n] = '\0';
    rc = fl_fs_ramfs_cat(session, name, existing, sizeof existing);
    /* Replace a ramfs file only when it is this file's earlier delivered copy. */
    if (rc == 0 && !(previous->data && previous->len == strlen(existing) &&
                     memcmp(previous->data, existing, previous->len) == 0)) {
        say("keyquorum: %s exists in this directory; the delivered copy is kept in the "
            "KeyQuorum store", name, NULL, NULL);
        mem_domain_secure_zero(content, sizeof content);
        return;
    }
    if (fl_fs_ramfs_write(session, name, content) == 0)
        lab_kq_event_js("received", name, "", "");
    mem_domain_secure_zero(content, sizeof content);
    mem_domain_secure_zero(existing, sizeof existing);
}

/* Decode checkpoint for a released letter, then ACK; as the native shell's
 * kq_decode_and_ack. Returns 1 when finished (either way), 0 to try again later. */
static int decode_and_ack(pending_open_t *p) {
    fl_net_kq_inbox_t in;
    fl_net_kq_keys_t sender;
    fl_kq_buf_t letter = {0};
    fl_kq_buf_t previous = {0};
    lab_key_t *me = local_keys(p->me);
    char dir[256];
    char dest[384];
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    fl_kq_cp_result_t cp;
    fl_result_t rc;

    if (find_request(p->id, &in) != 0)
        return 1;
    if (!in.letter_ready)
        return 0;
    rc = fl_net_kq_keys_cached(in.sender, &sender);
    if (rc == FL_RESULT_TIMEDOUT)
        return 0;
    if (rc != FL_RESULT_OK || strcmp(sender.key_id, in.sender) != 0 || !me) {
        checkpoint("decode", "refused", "the sender's key is not on the server");
        return 1;
    }
    if (!fl_kq_cp_safe_name(in.file_name)) {
        checkpoint("decode", "refused", "the file name is not a plain name");
        return 1;
    }
    if (fl_net_kq_take_letter(p->id, 0, &letter) != FL_RESULT_OK)
        return 0;
    snprintf(dir, sizeof dir, LAB_KQ_ROOT "/%s/in", p->me);
    snprintf(dest, sizeof dest, "%s/%s", dir, in.file_name);
    (void)fl_fs_mkdir_p(dir, 0700);
    (void)fl_fs_read_file(dest, FL_KQ_LETTER_MAX, &previous.data, &previous.len);
    cp = fl_kq_cp_decode_with(lab_directory(), in.sender, sender.signing_public, p->me,
                              me->box_sec, &letter, &in.offer, in.sig, dest);
    fl_kq_buf_free(&letter);
    fl_kq_delivery_id_hex(p->id, hex);
    if (cp == FL_KQ_CP_EXISTS || cp == FL_KQ_CP_IO) {
        /* A local problem with the destination, not with the letter: keep it to retry. */
        checkpoint("decode", "not saved", fl_kq_cp_reason(cp));
        say("keyquorum: retry with: server file open %s", hex, NULL, NULL);
        fl_kq_buf_free(&previous);
        return 1;
    }
    (void)fl_net_kq_send_ack(s_srv, NULL, s_hosting, p->id, cp == FL_KQ_CP_OK);
    fl_net_kq_inbox_drop(p->id);
    if (cp != FL_KQ_CP_OK) {
        checkpoint("decode", "refused", fl_kq_cp_reason(cp));
        fl_kq_buf_free(&previous);
        return 1;
    }
    checkpoint("decode", "ok", "author signature, trust, your co-signature, single head, newer");
    say("keyquorum: %s from %s decoded and verified", in.file_name, in.sender, NULL);
    write_ramfs_copy(p->session, in.file_name, &previous, dest);
    fl_kq_buf_free(&previous);
    return 1;
}

static void encode_and_send(pending_send_t *p, const fl_net_kq_keys_t *to) {
    lab_key_t *me = local_keys(p->me);
    fl_kq_encoded_t enc;
    char when[32];
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    fl_kq_cp_result_t cp;

    if (!me) {
        checkpoint("encode", "refused", "this account has no keys");
        return;
    }
    if (p->permitted) {
        /* A permitted newer copy: the head the supervisor countersigned, sealed as it is. */
        uint8_t head[32];

        cp = fl_kq_cp_head(p->path, head);
        if (cp == FL_KQ_CP_OK)
            cp = fl_kq_cp_reseal_with(lab_directory(), p->me, me->sign_sec, to->key_id,
                                      to->box_public, p->path, head, &enc);
    } else {
        utc_now(when);
        cp = fl_kq_cp_encode_with(lab_directory(), p->me, me->sign_sec, to->key_id,
                                  to->box_public, p->path, when, &enc);
    }
    if (cp != FL_KQ_CP_OK) {
        checkpoint("encode", "refused", fl_kq_cp_reason(cp));
        return;
    }
    checkpoint("encode", "ok", "checked in, signed by you, sealed to the recipient");
    fl_kq_delivery_id_hex(enc.delivery_id, hex);
    if (fl_net_kq_send_delivery(s_srv, NULL, s_hosting, p->me, to->key_id, &enc) != FL_RESULT_OK)
        say("keyquorum: the offer could not be sent", NULL, NULL, NULL);
    else
        say("keyquorum: offered %s to %s (delivery %s); released once they co-sign",
            enc.file_name, to->key_id, hex);
    fl_kq_encoded_free(&enc);
}

/* New co-signature requests, for the panel (the shell already printed them). */
static void announce_new_requests(void) {
    fl_net_kq_inbox_t all[LAB_INBOX_MAX];
    size_t n = fl_net_kq_inbox(all, LAB_INBOX_MAX);
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];

    for (size_t i = 0; i < n; i++) {
        size_t j;
        for (j = 0; j < s_announced_n; j++)
            if (memcmp(s_announced[j], all[i].id, FL_KQ_DELIVERY_ID_LEN) == 0)
                break;
        if (j < s_announced_n)
            continue;
        if (s_announced_n == LAB_INBOX_MAX) {
            memmove(s_announced[0], s_announced[1], (LAB_INBOX_MAX - 1) * FL_KQ_DELIVERY_ID_LEN);
            s_announced_n--;
        }
        memcpy(s_announced[s_announced_n++], all[i].id, FL_KQ_DELIVERY_ID_LEN);
        fl_kq_delivery_id_hex(all[i].id, hex);
        lab_kq_event_js("request", hex, all[i].sender, all[i].file_name);
    }
}

/* Moves every waiting step on as far as the answers that have arrived allow. */
static void progress(void) {
    for (int i = 0; i < LAB_PENDING_MAX; i++) {
        fl_net_kq_keys_t keys;
        fl_result_t rc;

        if (!s_sends[i].used)
            continue;
        rc = fl_net_kq_keys_cached(s_sends[i].to, &keys);
        if (rc == FL_RESULT_TIMEDOUT && now_sec() - s_sends[i].asked_at < LAB_SEND_WAIT_SEC)
            continue;
        if (rc == FL_RESULT_OK)
            encode_and_send(&s_sends[i], &keys);
        else if (rc == FL_RESULT_NOENT)
            say("keyquorum: the server holds no keys for %s (they must be connected once)",
                s_sends[i].to, NULL, NULL);
        else
            say("keyquorum: no answer from the server about %s", s_sends[i].to, NULL, NULL);
        mem_domain_secure_zero(&s_sends[i], sizeof s_sends[i]);
    }
    for (int i = 0; i < LAB_PENDING_MAX; i++)
        if (s_opens[i].used && decode_and_ack(&s_opens[i]))
            memset(&s_opens[i], 0, sizeof s_opens[i]);
    announce_new_requests();
}

/* ---- verbs ------------------------------------------------------------------------------- */

static int connected(void) {
    if (self_member())
        return 1;
    say("keyquorum: not connected to the server (server join, or wait for the lab to join)",
        NULL, NULL, NULL);
    return 0;
}

int fl_lab_kq_send(int session, const char *path, const char *user, int permitted) {
    const char *me = fl_fs_identity_user(session);
    const char *name = fl_fs_basename(path);
    char content[FL_FS_RAMFS_DATA];
    char dir[256];
    pending_send_t *p = NULL;
    fl_net_kq_keys_t ignored;
    int rc;

    if (!connected())
        return 1;
    if (!fl_kq_cp_safe_name(name) || strlen(name) >= FL_FS_RAMFS_PATH) {
        say("keyquorum: the file name must be one plain name", NULL, NULL, NULL);
        return 1;
    }
    rc = fl_fs_ramfs_cat(session, path, content, sizeof content);
    if (rc != 0) {
        say("keyquorum: %s: not found", path, NULL, NULL);
        return 1;
    }
    if (!local_keys(me)) {
        checkpoint("encode", "refused", "this account has no keys (its name is held by another member)");
        return 1;
    }
    for (int i = 0; i < LAB_PENDING_MAX && !p; i++)
        if (!s_sends[i].used)
            p = &s_sends[i];
    if (!p) {
        say("keyquorum: too many sends waiting for the server", NULL, NULL, NULL);
        return 1;
    }
    memset(p, 0, sizeof *p);
    snprintf(dir, sizeof dir, LAB_KQ_ROOT "/%s/out", me);
    snprintf(p->path, sizeof p->path, "%s/%s", dir, name);
    /* The tracked copy: its container sits beside it, so each send is a new revision. */
    if (fl_fs_mkdir_p(dir, 0700) != 0 ||
        fl_fs_write_file_atomic(p->path, (const uint8_t *)content, strlen(content)) != 0) {
        mem_domain_secure_zero(content, sizeof content);
        say("keyquorum: could not stage %s", name, NULL, NULL);
        return 1;
    }
    mem_domain_secure_zero(content, sizeof content);
    p->session = session;
    snprintf(p->me, sizeof p->me, "%s", me);
    snprintf(p->to, sizeof p->to, "%s", user);
    snprintf(p->name, sizeof p->name, "%s", name);
    p->asked_at = now_sec();
    p->permitted = permitted;
    p->used = 1;
    /* Ask the host for the recipient's keys; the answer moves the send on. */
    (void)fl_net_kq_lookup(s_srv, NULL, s_hosting, user, 0, &ignored);
    say("keyquorum: asking the server for %s's keys...", user, NULL, NULL);
    progress();
    return 1;
}

/* One history line, formatted, for fl_kq_cp_for_each_event. */
static int say_event_line(const char *line, size_t n, void *ctx) {
    char text[384];

    (void)ctx;
    fl_kq_cp_format_event(line, n, text, sizeof text);
    say("%s", text, NULL, NULL);
    return 0;
}

/* server file log <path>: the history of a tracked copy, read from its container. Offline: it
 * needs no server and no key. */
int fl_lab_kq_log(int session, const char *path) {
    fl_kq_buf_t lines = {0};
    fl_kq_cp_result_t cp;

    (void)session;
    cp = fl_kq_cp_history(path, &lines);
    if (cp != FL_KQ_CP_OK) {
        say("server file log: %s", fl_kq_cp_reason(cp), NULL, NULL);
        fl_kq_buf_free(&lines);
        return 1;
    }
    if (fl_kq_cp_for_each_event(&lines, say_event_line, NULL) == 0u)
        say("no history for %s", fl_fs_basename(path), NULL, NULL);
    fl_kq_buf_free(&lines);
    return 1;
}

/* server file permit <path>: the signed-in supervisor countersigns the head of a tracked copy.
 * A recipient counts the permit only when the signer is the file's scope owner or an ancestor. */
int fl_lab_kq_permit(int session, const char *path) {
    const char *me = fl_fs_identity_user(session);
    lab_key_t *keys = me ? local_keys(me) : NULL;
    uint8_t head[32];
    fl_kq_cp_result_t cp;

    if (!keys) {
        say("server file permit: sign in as the supervisor first", NULL, NULL, NULL);
        return 1;
    }
    cp = fl_kq_cp_head(path, head);
    if (cp != FL_KQ_CP_OK) {
        say("server file permit: %s", fl_kq_cp_reason(cp), NULL, NULL);
        return 1;
    }
    cp = fl_kq_cp_permit_with(lab_directory(), me, keys->sign_sec, path, head);
    if (cp != FL_KQ_CP_OK) {
        say("server file permit: %s", fl_kq_cp_reason(cp), NULL, NULL);
        return 1;
    }
    say("keyquorum: permitted the head of %s as %s", fl_fs_basename(path), me, NULL);
    return 1;
}

static void list_requests(void) {
    fl_net_kq_inbox_t all[LAB_INBOX_MAX];
    size_t n = fl_net_kq_inbox(all, LAB_INBOX_MAX);
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];

    if (n == 0) {
        say("keyquorum: no co-signature requests", NULL, NULL, NULL);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        fl_kq_delivery_id_hex(all[i].id, hex);
        say("  %s  %s from %s", hex, all[i].file_name, all[i].sender);
        say("    %s", all[i].letter_ready ? "released" : all[i].cosigned ? "co-signed"
                                                                         : "waiting for you",
            NULL, NULL);
    }
}

int fl_lab_kq_cosign(int session, const char *hex) {
    const char *me = fl_fs_identity_user(session);
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    fl_net_kq_inbox_t in;
    fl_net_kq_keys_t ignored;
    lab_key_t *keys;
    uint8_t sig[64];
    fl_kq_cp_result_t cp;
    pending_open_t *p = NULL;

    if (!hex || !hex[0]) {
        list_requests();
        return 1;
    }
    if (!connected())
        return 1;
    if (fl_kq_delivery_id_parse(hex, id) != 0 || find_request(id, &in) != 0) {
        say("keyquorum: no co-signature request %s (run: server file cosign)", hex, NULL, NULL);
        return 1;
    }
    keys = local_keys(me);
    if (!keys) {
        checkpoint("co-sign", "refused", "this account has no keys");
        return 1;
    }
    cp = fl_kq_cp_cosign(&in.offer, me, keys->sign_sec, sig);
    if (cp != FL_KQ_CP_OK) {
        checkpoint("co-sign", "refused", fl_kq_cp_reason(cp));
        return 1;
    }
    for (int i = 0; i < LAB_PENDING_MAX && !p; i++)
        if (!s_opens[i].used)
            p = &s_opens[i];
    if (!p) {
        say("keyquorum: too many deliveries waiting", NULL, NULL, NULL);
        return 1;
    }
    /* Recorded only after it is sent, so a failed send can simply be co-signed again. */
    if (fl_net_kq_send_cosign(s_srv, NULL, s_hosting, id, me, sig) != FL_RESULT_OK) {
        say("keyquorum: the co-signature could not be sent; run cosign again", NULL, NULL, NULL);
        return 1;
    }
    (void)fl_net_kq_inbox_set_cosig(id, sig);
    checkpoint("co-sign", "ok", "you signed the offered revision");
    memset(p, 0, sizeof *p);
    p->session = session;
    snprintf(p->me, sizeof p->me, "%s", me);
    memcpy(p->id, id, sizeof id);
    p->used = 1;
    (void)fl_net_kq_lookup(s_srv, NULL, s_hosting, in.sender, 0, &ignored);
    say("keyquorum: co-signed %s; it opens when the server releases it", in.file_name, NULL, NULL);
    progress();
    return 1;
}

int fl_lab_kq_open(int session, const char *hex) {
    const char *me = fl_fs_identity_user(session);
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    fl_net_kq_inbox_t in;
    fl_net_kq_keys_t ignored;
    pending_open_t *p = NULL;

    if (!connected())
        return 1;
    if (!hex || fl_kq_delivery_id_parse(hex, id) != 0 || find_request(id, &in) != 0) {
        say("keyquorum: no co-signature request %s (run: server file cosign)", hex, NULL, NULL);
        return 1;
    }
    if (!in.cosigned) {
        say("keyquorum: co-sign it first: server file cosign %s", hex, NULL, NULL);
        return 1;
    }
    for (int i = 0; i < LAB_PENDING_MAX && !p; i++)
        if (!s_opens[i].used || memcmp(s_opens[i].id, id, sizeof id) == 0)
            p = &s_opens[i];
    if (!p) {
        say("keyquorum: too many deliveries waiting", NULL, NULL, NULL);
        return 1;
    }
    /* The host releases again to a recipient that has co-signed. */
    (void)fl_net_kq_send_cosign(s_srv, NULL, s_hosting, id, me, in.sig);
    memset(p, 0, sizeof *p);
    p->session = session;
    snprintf(p->me, sizeof p->me, "%s", me);
    memcpy(p->id, id, sizeof id);
    p->used = 1;
    (void)fl_net_kq_lookup(s_srv, NULL, s_hosting, in.sender, 0, &ignored);
    progress();
    return 1;
}

void fl_lab_kq_init(void (*emit)(const char *line)) {
    s_emit = emit;
    (void)fl_fs_mkdir_p(LAB_HOST_DIR, 0700);
    (void)fl_net_kq_configure(lab_directory, LAB_HOST_DIR);
    fl_net_kq_set_transport(host_send, client_send);
}

/* ---- the page ------------------------------------------------------------------------- */

/*
 * The relay roster: entries of member id (u16 big-endian), is_host (u8), principal length
 * (u8) and principal. self_id is this page's member.
 */
EMSCRIPTEN_KEEPALIVE void wasm_kq_roster(int self_id, const uint8_t *buf, int len) {
    size_t off = 0;

    s_member_count = 0;
    memset(s_members, 0, sizeof s_members);
    while (buf && off + 4 <= (size_t)len && s_member_count < FL_NET_SERVER_MAX_MEMBERS) {
        fl_net_server_member_t *m = &s_members[s_member_count];
        size_t plen = buf[off + 3];

        if (off + 4 + plen > (size_t)len || plen == 0 || plen >= sizeof m->principal)
            break;
        m->member_id = (fl_net_server_member_id_t)((buf[off] << 8) | buf[off + 1]);
        m->is_host = buf[off + 2] ? 1 : 0;
        m->in_use = 1;
        m->peer_handle = FL_NET_SOCK_INVALID;
        memcpy(m->principal, buf + off + 4, plen);
        s_member_count++;
        off += 4 + plen;
    }
    s_self_id = (fl_net_server_member_id_t)self_id;
    s_hosting = 0;
    for (size_t i = 0; i < s_member_count; i++)
        if (s_members[i].member_id == s_self_id && s_members[i].is_host)
            s_hosting = 1;
    progress();
}

/* A frame from the relay: to this page's host (from member_id) or to its client side. */
EMSCRIPTEN_KEEPALIVE void wasm_kq_frame(int to_host, int from_id, int op, const uint8_t *p,
                                        int n) {
    if (n < 0 || n > 0xffff)
        return;
    if (to_host) {
        const fl_net_server_member_t *from = NULL;
        for (size_t i = 0; i < s_member_count; i++)
            if (s_members[i].member_id == (fl_net_server_member_id_t)from_id)
                from = &s_members[i];
        if (s_hosting && from)
            fl_net_kq_host_handle(s_srv, from, (uint8_t)op, p, (uint16_t)n);
    } else if (op == FL_NET_SESSION_OP_ERR) {
        char text[256];
        size_t len = (size_t)n < sizeof text - 1 ? (size_t)n : sizeof text - 1;

        memcpy(text, p, len);
        text[len] = '\0';
        checkpoint("host", "refused", text);
    } else {
        fl_net_kq_client_handle(NULL, (uint8_t)op, p, (uint16_t)n);
    }
    progress();
}

/*
 * The keys a member published, for the host's directory. The first keys seen for a name are
 * kept: different keys for a name already known are refused (0), the same keys are fine (1).
 */
EMSCRIPTEN_KEEPALIVE int wasm_kq_learn(const char *label, const uint8_t sign[32],
                                       const uint8_t box[32]) {
    lab_key_t *k;

    if (!label || !label[0] || strlen(label) >= LAB_LABEL_MAX)
        return 0;
    k = key_find(label);
    if (k)
        return memcmp(k->sign_pub, sign, 32) == 0 && memcmp(k->box_pub, box, 32) == 0;
    if (!(k = key_slot()))
        return 0;
    memset(k, 0, sizeof *k);
    snprintf(k->label, sizeof k->label, "%s", label);
    memcpy(k->sign_pub, sign, 32);
    memcpy(k->box_pub, box, 32);
    k->used = 1;
    return 1;
}

/* This lab account's public keys (signing then box, 64 bytes), made on first use; NULL when
 * the name is already held by another member's keys. */
EMSCRIPTEN_KEEPALIVE const uint8_t *wasm_kq_public(const char *label) {
    static uint8_t out[64];
    lab_key_t *k = local_keys(label);

    if (!k)
        return NULL;
    memcpy(out, k->sign_pub, 32);
    memcpy(out + 32, k->box_pub, 32);
    return out;
}

/* Called by the page now and then, so an unanswered send is reported and requests leak. */
EMSCRIPTEN_KEEPALIVE void wasm_kq_tick(void) {
    progress();
}
