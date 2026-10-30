#include "net_kq_delivery.h"

#include "net_codec.h"
#include "net_endian.h"
#include "net_part_buf.h"
#include "contract_p3_session_wire.h"
#include "kq_checkpoint.h"
#include "mem_domain.h"
#include "timekeeping.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Letter bytes per PART frame: 512 - id(16) - offset(4), rounded down. */
#define KQ_PART_MAX 480u
#define KQ_INBOX_MAX 16u
#define KQ_KEYS_MAX 16u
#define KQ_UPLOADS_MAX 8u
/* An upload that receives no part for this long is dropped, so a member that offers and
 * goes quiet (or disconnects) cannot hold a slot. Parts follow the offer immediately. */
#define KQ_UPLOAD_IDLE_SEC 60

/* Recursive: the host's own user reaches the client side from inside a host handler. */
static pthread_mutex_t s_lock;
static pthread_once_t s_lock_once = PTHREAD_ONCE_INIT;

static void lock_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

static void kq_lock(void) {
    pthread_once(&s_lock_once, lock_init);
    pthread_mutex_lock(&s_lock);
}

static void kq_unlock(void) {
    pthread_mutex_unlock(&s_lock);
}
static fl_net_kq_keys_fn s_authority;
static char s_store_dir[1024];
static fl_kq_store_t *s_store;
static fl_net_kq_host_send_fn s_host_send;
static fl_net_kq_client_send_fn s_client_send;
static int64_t (*s_now)(void);

/* ---- wire codec ------------------------------------------------------------ */

/* The codec is net_codec.h: frames are built with its sticky writer (one check per frame)
 * and read with its sticky reader. The helpers below only add the KQ value conventions: a
 * short read yields 0, and an offer is a fixed run of fields. */
/* The writer's capacity and every frame length are uint16_t; the cap may be overridden. */
_Static_assert(FL_NET_SESSION_MAX_MSG <= UINT16_MAX, "session message cap must fit uint16_t");

static uint32_t r_u32(fl_bytes_reader_t *r) {
    uint32_t v = 0;
    (void)fl_bytes_reader_read_u32(r, &v);
    return v;
}

static uint64_t r_u64(fl_bytes_reader_t *r) {
    uint64_t v = 0;
    (void)fl_bytes_reader_read_u64(r, &v);
    return v;
}

static void kq_put_offer(fl_bytes_sticky_writer_t *w, const kq_file_offer_t *o) {
    (void)fl_bytes_sticky_put_bytes(w, o->file_id, 16);
    (void)fl_bytes_sticky_put_bytes(w, o->revision_id, 32);
    (void)fl_bytes_sticky_put_bytes(w, o->content_commitment, 32);
    (void)fl_bytes_sticky_put_bytes(w, o->author_identity, 16);
    (void)fl_bytes_sticky_put_bytes(w, o->author_signature, 64);
    (void)fl_bytes_sticky_put_u64(w, o->topology_generation);
    (void)fl_bytes_sticky_put_bytes(w, o->policy_hash, 32);
}

static void kq_get_offer(fl_bytes_reader_t *r, kq_file_offer_t *o) {
    (void)fl_bytes_reader_read_bytes(r, o->file_id, 16);
    (void)fl_bytes_reader_read_bytes(r, o->revision_id, 32);
    (void)fl_bytes_reader_read_bytes(r, o->content_commitment, 32);
    (void)fl_bytes_reader_read_bytes(r, o->author_identity, 16);
    (void)fl_bytes_reader_read_bytes(r, o->author_signature, 64);
    o->topology_generation = r_u64(r);
    (void)fl_bytes_reader_read_bytes(r, o->policy_hash, 32);
}

/* ---- shared ---------------------------------------------------------------- */

static int64_t now_sec(void) {
    int64_t t = 0;
    if (s_now)
        return s_now();
    if (fl_time_wall_sec(&t) != FL_RESULT_OK)
        t = (int64_t)time(NULL);
    return t;
}

/* The host's key authority: the native account store, or the lab's directory. */
static const fl_kq_keydir_t *keys(void) {
    return s_authority ? s_authority() : NULL;
}

/* The account a member signs in as, by the host's registry. */
static const char *key_of_member(const fl_net_server_member_t *m) {
    const fl_kq_keydir_t *k = keys();

    return (m && k) ? k->resolve(k, m->principal) : NULL;
}

static fl_kq_store_t *store(void) {
    if (!s_store && s_store_dir[0])
        (void)fl_kq_store_open(s_store_dir, &s_store);
    return s_store;
}

static void notice(const char *fmt, const char *a, const char *b) {
    printf("[keyquorum] ");
    printf(fmt, a ? a : "", b ? b : "");
    printf("\n");
    fflush(stdout);
}

fl_result_t fl_net_kq_configure(fl_net_kq_keys_fn keys_fn, const char *store_dir) {
    if (!keys_fn || !store_dir || strlen(store_dir) >= sizeof s_store_dir)
        return FL_RESULT_INVAL;
    kq_lock();
    s_authority = keys_fn;
    snprintf(s_store_dir, sizeof s_store_dir, "%s", store_dir);
    kq_unlock();
    return FL_RESULT_OK;
}

void fl_net_kq_set_transport(fl_net_kq_host_send_fn host_send,
                             fl_net_kq_client_send_fn client_send) {
    s_host_send = host_send;
    s_client_send = client_send;
}

void fl_net_kq_set_clock(int64_t (*now)(void)) {
    s_now = now;
}

/* ---- host ------------------------------------------------------------------- */

/* A letter being uploaded: its parts (base fl_part_buf_t, limited to the offered length) and
 * the offer and member it belongs to. */
typedef struct {
    fl_part_buf_t             letter;
    int                       used;
    fl_net_server_member_id_t from;
    uint8_t                   id[FL_KQ_DELIVERY_ID_LEN];
    char                      author[FL_HERITAGE_KEY_ID_MAX];
    char                      recipient[FL_HERITAGE_KEY_ID_MAX];
    char                      file_name[128];
    kq_file_offer_t           offer;
    int64_t                   last_at;
} kq_upload_t;

static kq_upload_t s_uploads[KQ_UPLOADS_MAX];

static void host_send(fl_net_server_t *srv, const fl_net_server_member_t *to, uint8_t opcode,
                      const uint8_t *payload, uint16_t plen) {
    if (s_host_send) {
        s_host_send(srv, to, opcode, payload, plen);
        return;
    }
    /* The host's own user has no socket: deliver to this shell's client side. */
    if (to->is_host || to->peer_handle == FL_NET_SOCK_INVALID) {
        fl_net_kq_client_handle(NULL, opcode, payload, plen);
        return;
    }
    (void)fl_net_session_send_frame(to->peer_handle, opcode, payload, plen);
}

static void host_refuse(fl_net_server_t *srv, const fl_net_server_member_t *to, const char *what,
                        const char *why) {
    char text[FL_NET_SESSION_MAX_MSG];
    int n = snprintf(text, sizeof text, "keyquorum: %s refused: %s", what, why);

    if (n <= 0)
        return;
    if ((size_t)n >= sizeof text)
        n = (int)sizeof text - 1;
    if (!s_host_send && (to->is_host || to->peer_handle == FL_NET_SOCK_INVALID)) {
        printf("%s\n", text);
        fflush(stdout);
        return;
    }
    host_send(srv, to, (uint8_t)FL_NET_SESSION_OP_ERR, (const uint8_t *)text, (uint16_t)n);
}

/* Every connected member signed in as `key_id`. */
static void host_send_to_account(fl_net_server_t *srv, const char *key_id, uint8_t opcode,
                                 const uint8_t *payload, uint16_t plen) {
    size_t count = fl_net_server_member_count(srv);
    for (size_t i = 0; i < count; i++) {
        const fl_net_server_member_t *m = fl_net_server_member_at(srv, i);
        const char *k = key_of_member(m);
        if (m && k && strcmp(k, key_id) == 0)
            host_send(srv, m, opcode, payload, plen);
    }
}

static void host_announce(fl_net_server_t *srv, const fl_net_server_member_t *to,
                          const fl_kq_delivery_t *d) {
    uint8_t wbuf[FL_NET_SESSION_MAX_MSG];
    fl_bytes_sticky_writer_t w;
    fl_bytes_sticky_init(&w, wbuf, (uint16_t)sizeof wbuf);

    (void)fl_bytes_sticky_put_bytes(&w, d->id, FL_KQ_DELIVERY_ID_LEN);
    (void)fl_bytes_sticky_put_u64(&w, (uint64_t)d->expires_at);
    (void)fl_bytes_sticky_put_str8(&w, d->sender);
    (void)fl_bytes_sticky_put_str8(&w, d->file_name);
    kq_put_offer(&w, &d->offer);
    if (w.bad)
        return;
    if (to)
        host_send(srv, to, (uint8_t)FL_NET_SESSION_OP_KQ_ANNOUNCE, wbuf, (uint16_t)w.w.len);
    else
        host_send_to_account(srv, d->recipient, (uint8_t)FL_NET_SESSION_OP_KQ_ANNOUNCE, wbuf,
                             (uint16_t)w.w.len);
}

/* RELEASE header then the letter in parts, to `to` or every member of the recipient. */
static void host_release(fl_net_server_t *srv, const fl_net_server_member_t *to,
                         const fl_kq_delivery_t *d) {
    uint8_t *letter = NULL;
    size_t len = 0;
    uint8_t wbuf[FL_NET_SESSION_MAX_MSG];
    fl_bytes_sticky_writer_t w;
    fl_bytes_sticky_init(&w, wbuf, (uint16_t)sizeof wbuf);

    if (fl_kq_store_letter(store(), d->id, &letter, &len) != FL_RESULT_OK)
        return;
    (void)fl_bytes_sticky_put_bytes(&w, d->id, FL_KQ_DELIVERY_ID_LEN);
    (void)fl_bytes_sticky_put_u32(&w, (uint32_t)len);
    if (to)
        host_send(srv, to, (uint8_t)FL_NET_SESSION_OP_KQ_RELEASE, wbuf, (uint16_t)w.w.len);
    else
        host_send_to_account(srv, d->recipient, (uint8_t)FL_NET_SESSION_OP_KQ_RELEASE, wbuf,
                             (uint16_t)w.w.len);
    for (size_t off = 0; off < len; off += KQ_PART_MAX) {
        size_t n = len - off < KQ_PART_MAX ? len - off : KQ_PART_MAX;
        uint8_t pbuf[FL_NET_SESSION_MAX_MSG];
        fl_bytes_sticky_writer_t p;
        fl_bytes_sticky_init(&p, pbuf, (uint16_t)sizeof pbuf);
        (void)fl_bytes_sticky_put_bytes(&p, d->id, FL_KQ_DELIVERY_ID_LEN);
        (void)fl_bytes_sticky_put_u32(&p, (uint32_t)off);
        (void)fl_bytes_sticky_put_bytes(&p, letter + off, n);
        if (p.bad)
            break;
        if (to)
            host_send(srv, to, (uint8_t)FL_NET_SESSION_OP_KQ_PART, pbuf, (uint16_t)p.w.len);
        else
            host_send_to_account(srv, d->recipient, (uint8_t)FL_NET_SESSION_OP_KQ_PART, pbuf,
                                 (uint16_t)p.w.len);
    }
    mem_domain_free(MEM_DOMAIN_FS, letter);
}

static kq_upload_t *upload_find(fl_net_server_member_id_t from, const uint8_t *id) {
    for (size_t i = 0; i < KQ_UPLOADS_MAX; i++)
        if (s_uploads[i].used && s_uploads[i].from == from &&
            memcmp(s_uploads[i].id, id, FL_KQ_DELIVERY_ID_LEN) == 0)
            return &s_uploads[i];
    return NULL;
}

static void upload_free(kq_upload_t *u) {
    fl_part_buf_release(&u->letter);
    memset(u, 0, sizeof *u);
}

/* Drops uploads idle past KQ_UPLOAD_IDLE_SEC on the host clock. */
static void upload_reclaim(int64_t now) {
    for (size_t i = 0; i < KQ_UPLOADS_MAX; i++)
        if (s_uploads[i].used && now - s_uploads[i].last_at > KQ_UPLOAD_IDLE_SEC)
            upload_free(&s_uploads[i]);
}

static void host_offer(fl_net_server_t *srv, const fl_net_server_member_t *from,
                       fl_bytes_reader_t *r) {
    kq_upload_t up;
    uint32_t len;
    const char *me = key_of_member(from);
    fl_kq_cp_result_t cp;
    fl_kq_delivery_t existing;

    memset(&up, 0, sizeof up);
    (void)fl_bytes_reader_read_bytes(r, up.id, FL_KQ_DELIVERY_ID_LEN);
    len = r_u32(r);
    (void)fl_bytes_reader_read_str8(r, up.author, sizeof up.author);
    (void)fl_bytes_reader_read_str8(r, up.recipient, sizeof up.recipient);
    (void)fl_bytes_reader_read_str8(r, up.file_name, sizeof up.file_name);
    kq_get_offer(r, &up.offer);
    if (r->bad || fl_bytes_reader_remaining(r) != 0 || len == 0 || len > FL_KQ_LETTER_MAX ||
        !fl_kq_cp_safe_name(up.file_name)) {
        host_refuse(srv, from, "delivery", "malformed offer");
        return;
    }
    /* A member offers only as the account it signed in as; the signature decides. */
    if (!me || strcmp(me, up.author) != 0) {
        host_refuse(srv, from, "delivery", "offer is not from this member's account");
        return;
    }
    /* The recipient is named by key id and must hold a box key the letter can be sealed to. */
    if (!keys()->box_public(keys(), up.recipient)) {
        host_refuse(srv, from, "delivery", fl_kq_cp_reason(FL_KQ_CP_NO_KEY));
        return;
    }
    cp = fl_kq_cp_admit_offer_with(keys(), up.author, &up.offer);
    if (cp != FL_KQ_CP_OK) {
        host_refuse(srv, from, "delivery", fl_kq_cp_reason(cp));
        return;
    }
    if (store() && fl_kq_store_get(store(), up.id, &existing) == FL_RESULT_OK) {
        host_refuse(srv, from, "delivery", "delivery id already held");
        return;
    }
    /* One upload per member: a new offer replaces the member's unfinished one (a retry). An id
     * another member is still uploading is refused. */
    upload_reclaim(now_sec());
    for (size_t i = 0; i < KQ_UPLOADS_MAX; i++) {
        if (!s_uploads[i].used)
            continue;
        if (s_uploads[i].from == from->member_id) {
            upload_free(&s_uploads[i]);
        } else if (memcmp(s_uploads[i].id, up.id, FL_KQ_DELIVERY_ID_LEN) == 0) {
            host_refuse(srv, from, "delivery", "delivery id already in flight");
            return;
        }
    }
    for (size_t i = 0; i < KQ_UPLOADS_MAX; i++) {
        if (s_uploads[i].used)
            continue;
        fl_part_buf_init(&up.letter, len);
        if (fl_part_buf_reserve(&up.letter, len) != FL_RESULT_OK)
            break;
        up.used = 1;
        up.from = from->member_id;
        up.last_at = now_sec();
        s_uploads[i] = up;
        return;
    }
    host_refuse(srv, from, "delivery", "host is busy; try again");
}

static void host_part(fl_net_server_t *srv, const fl_net_server_member_t *from,
                      fl_bytes_reader_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint16_t rest;
    uint32_t off;
    kq_upload_t *u;
    fl_kq_delivery_t d;
    fl_result_t rc;

    (void)fl_bytes_reader_read_bytes(r, id, sizeof id);
    off = r_u32(r);
    upload_reclaim(now_sec());
    u = r->bad ? NULL : upload_find(from->member_id, id);
    if (!u)
        return;
    u->last_at = now_sec();
    rest = fl_bytes_reader_remaining(r);
    if (rest == 0 ||
        fl_part_buf_append(&u->letter, off, fl_bytes_reader_cursor(r), rest) != FL_RESULT_OK) {
        upload_free(u);
        host_refuse(srv, from, "delivery", "letter parts out of order");
        return;
    }
    if (!fl_part_buf_complete(&u->letter))
        return;
    /* Complete: the latent space holds it, on the host's clock, and asks the recipient. */
    fl_kq_delivery_init(&d, u->id, u->author, u->recipient, u->file_name, &u->offer, now_sec(),
                        FL_KQ_DELIVERY_TTL_SEC);
    rc = store() ? fl_kq_store_put(store(), &d, u->letter.data, u->letter.len) : FL_RESULT_ERR;
    upload_free(u);
    if (rc != FL_RESULT_OK) {
        host_refuse(srv, from, "delivery", "host could not hold the letter");
        return;
    }
    host_announce(srv, NULL, &d);
}

static void host_pull(fl_net_server_t *srv, const fl_net_server_member_t *from) {
    fl_kq_delivery_t pending[8];
    const char *me = key_of_member(from);
    size_t n;

    if (!me || !store())
        return;
    n = fl_kq_store_pending_for(store(), me, now_sec(), pending, 8);
    for (size_t i = 0; i < n; i++) {
        host_announce(srv, from, &pending[i]);
        if (pending[i].fired)
            host_release(srv, from, &pending[i]);
    }
}

static void host_cosign(fl_net_server_t *srv, const fl_net_server_member_t *from,
                        fl_bytes_reader_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    char coauthor[FL_HERITAGE_KEY_ID_MAX];
    uint8_t sig[64];
    fl_kq_delivery_t d;
    const char *me = key_of_member(from);
    fl_kq_cp_result_t cp;
    int64_t t = now_sec();

    (void)fl_bytes_reader_read_bytes(r, id, sizeof id);
    (void)fl_bytes_reader_read_str8(r, coauthor, sizeof coauthor);
    (void)fl_bytes_reader_read_bytes(r, sig, sizeof sig);
    if (r->bad || fl_bytes_reader_remaining(r) != 0 || !store() ||
        fl_kq_store_get(store(), id, &d) != FL_RESULT_OK) {
        host_refuse(srv, from, "co-signature", "no such delivery");
        return;
    }
    if (strcmp(coauthor, d.recipient) != 0 || !me || strcmp(me, d.recipient) != 0) {
        host_refuse(srv, from, "co-signature", fl_kq_cp_reason(FL_KQ_CP_NOT_RECIPIENT));
        return;
    }
    cp = fl_kq_cp_admit_cosign_with(keys(), d.recipient, &d.offer, sig);
    if (cp != FL_KQ_CP_OK) {
        host_refuse(srv, from, "co-signature", fl_kq_cp_reason(cp));
        return;
    }
    switch (fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, t)) {
    case FL_KQ_NEURON_FIRE:
        if (fl_kq_store_update(store(), &d, FL_KQ_SPIKE_COSIGN, t) != FL_RESULT_OK) {
            host_refuse(srv, from, "co-signature", "host could not record it");
            return;
        }
        host_release(srv, from, &d);
        return;
    case FL_KQ_NEURON_REFRACTORY:
        /* Already released: send it again to this member (only they can open it), unless the
         * receipt is in or the host has ended it. */
        if (!d.acknowledged && !d.expired)
            host_release(srv, from, &d);
        return;
    case FL_KQ_NEURON_LEAKED:
        host_refuse(srv, from, "co-signature", "delivery expired");
        (void)fl_kq_store_leak(store(), t);
        return;
    case FL_KQ_NEURON_REST:
    default:
        (void)fl_kq_store_update(store(), &d, FL_KQ_SPIKE_COSIGN, t);
        return;
    }
}

static void host_ack(fl_net_server_t *srv, const fl_net_server_member_t *from,
                     fl_bytes_reader_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint8_t accepted = 0;
    fl_kq_delivery_t d;
    const char *me = key_of_member(from);

    (void)srv;
    (void)fl_bytes_reader_read_bytes(r, id, sizeof id);
    (void)fl_bytes_reader_read_bytes(r, &accepted, 1);
    if (r->bad || !store() || fl_kq_store_get(store(), id, &d) != FL_RESULT_OK || !me ||
        strcmp(me, d.recipient) != 0 || !d.fired || d.expired)
        return;
    d.acknowledged = 1;
    if (fl_kq_store_update(store(), &d, FL_KQ_SPIKE_ACK, now_sec()) == FL_RESULT_OK)
        (void)fl_kq_store_forget_letter(store(), id);
}

static void host_keys(fl_net_server_t *srv, const fl_net_server_member_t *from,
                      fl_bytes_reader_t *r) {
    char who[FL_HERITAGE_KEY_ID_MAX];
    const char *key;
    const unsigned char *sign = NULL;
    const unsigned char *box = NULL;
    uint8_t zero[32] = {0};
    uint8_t found;
    uint8_t wbuf[FL_NET_SESSION_MAX_MSG];
    fl_bytes_sticky_writer_t w;
    fl_bytes_sticky_init(&w, wbuf, (uint16_t)sizeof wbuf);

    (void)fl_bytes_reader_read_str8(r, who, sizeof who);
    if (r->bad)
        return;
    key = keys()->resolve(keys(), who);
    if (key) {
        sign = keys()->signing_public(keys(), key);
        box = keys()->box_public(keys(), key);
    }
    found = (key && sign && box) ? 1u : 0u;
    (void)fl_bytes_sticky_put_str8(&w, who);
    (void)fl_bytes_sticky_put_bytes(&w, &found, 1);
    (void)fl_bytes_sticky_put_str8(&w, found ? key : "");
    (void)fl_bytes_sticky_put_bytes(&w, found ? sign : zero, 32);
    (void)fl_bytes_sticky_put_bytes(&w, found ? box : zero, 32);
    if (!w.bad)
        host_send(srv, from, (uint8_t)FL_NET_SESSION_OP_KQ_KEYS, wbuf, (uint16_t)w.w.len);
}

void fl_net_kq_host_handle(fl_net_server_t *srv, const fl_net_server_member_t *from,
                           uint8_t opcode, const uint8_t *payload, uint16_t plen) {
    fl_bytes_reader_t r;
    fl_bytes_reader_init(&r, payload, plen);

    if (!from || !keys())
        return;
    kq_lock();
    if (store())
        (void)fl_kq_store_leak(store(), now_sec());
    switch (opcode) {
    case FL_NET_SESSION_OP_KQ_OFFER:
        host_offer(srv, from, &r);
        break;
    case FL_NET_SESSION_OP_KQ_PART:
        host_part(srv, from, &r);
        break;
    case FL_NET_SESSION_OP_KQ_ANNOUNCE:
        host_pull(srv, from);
        break;
    case FL_NET_SESSION_OP_KQ_COSIGN:
        host_cosign(srv, from, &r);
        break;
    case FL_NET_SESSION_OP_KQ_ACK:
        host_ack(srv, from, &r);
        break;
    case FL_NET_SESSION_OP_KQ_KEYS:
        host_keys(srv, from, &r);
        break;
    default:
        break;
    }
    kq_unlock();
}

/* ---- client ----------------------------------------------------------------- */

/* A co-signature request this shell holds: the released letter's parts (base fl_part_buf_t,
 * limited to the released length) and the request shown to the user. */
typedef struct {
    fl_part_buf_t     letter;
    int               used;
    fl_net_kq_inbox_t pub;
} kq_inbox_slot_t;

typedef struct {
    int              used;
    char             query[FL_HERITAGE_KEY_ID_MAX];
    fl_net_kq_keys_t keys;
} kq_keys_slot_t;

static kq_inbox_slot_t s_inbox[KQ_INBOX_MAX];
static kq_keys_slot_t s_keys[KQ_KEYS_MAX];

static kq_inbox_slot_t *inbox_find(const uint8_t *id) {
    for (size_t i = 0; i < KQ_INBOX_MAX; i++)
        if (s_inbox[i].used && memcmp(s_inbox[i].pub.id, id, FL_KQ_DELIVERY_ID_LEN) == 0)
            return &s_inbox[i];
    return NULL;
}

static void inbox_free(kq_inbox_slot_t *s) {
    fl_part_buf_release(&s->letter);
    memset(s, 0, sizeof *s);
}

static void client_announce(fl_bytes_reader_t *r) {
    fl_net_kq_inbox_t in;
    kq_inbox_slot_t *slot;
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];

    memset(&in, 0, sizeof in);
    (void)fl_bytes_reader_read_bytes(r, in.id, sizeof in.id);
    in.expires_at = (int64_t)r_u64(r);
    (void)fl_bytes_reader_read_str8(r, in.sender, sizeof in.sender);
    (void)fl_bytes_reader_read_str8(r, in.file_name, sizeof in.file_name);
    kq_get_offer(r, &in.offer);
    /* Names come from the wire: only plain ones are kept, printed or later joined to a path. */
    if (r->bad || fl_bytes_reader_remaining(r) != 0 || !fl_kq_cp_safe_name(in.file_name) ||
        !fl_kq_cp_safe_name(in.sender))
        return;
    slot = inbox_find(in.id);
    if (slot) {
        /* A repeat (pull) keeps what this shell already holds. */
        in.cosigned = slot->pub.cosigned;
        memcpy(in.sig, slot->pub.sig, sizeof in.sig);
        in.letter_ready = slot->pub.letter_ready;
        slot->pub = in;
        return;
    }
    for (size_t i = 0; i < KQ_INBOX_MAX; i++) {
        if (s_inbox[i].used)
            continue;
        s_inbox[i].used = 1;
        s_inbox[i].pub = in;
        fl_kq_delivery_id_hex(in.id, hex);
        notice("%s asks you to co-sign before it can be delivered: ", in.sender, NULL);
        printf("  %s  (delivery %s)\n  run: server file cosign %s\n", in.file_name, hex, hex);
        fflush(stdout);
        return;
    }
}

static void client_release(fl_bytes_reader_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint32_t len;
    kq_inbox_slot_t *slot;

    (void)fl_bytes_reader_read_bytes(r, id, sizeof id);
    len = r_u32(r);
    slot = r->bad ? NULL : inbox_find(id);
    if (!slot || len == 0 || len > FL_KQ_LETTER_MAX)
        return;
    /* A (re)release starts the letter again at offset 0. */
    fl_part_buf_release(&slot->letter);
    fl_part_buf_init(&slot->letter, len);
    if (fl_part_buf_reserve(&slot->letter, len) != FL_RESULT_OK)
        fl_part_buf_init(&slot->letter, 0);
    slot->pub.letter_ready = 0;
}

static void client_part(fl_bytes_reader_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint32_t off;
    kq_inbox_slot_t *slot;

    (void)fl_bytes_reader_read_bytes(r, id, sizeof id);
    off = r_u32(r);
    slot = r->bad ? NULL : inbox_find(id);
    if (!slot || !slot->letter.data)
        return;
    if (fl_part_buf_append(&slot->letter, off, fl_bytes_reader_cursor(r),
                           fl_bytes_reader_remaining(r)) != FL_RESULT_OK)
        return;
    if (fl_part_buf_complete(&slot->letter))
        slot->pub.letter_ready = 1;
}

static void client_keys(fl_bytes_reader_t *r) {
    char query[FL_HERITAGE_KEY_ID_MAX];
    fl_net_kq_keys_t k;
    uint8_t found = 0;
    kq_keys_slot_t *slot = NULL;

    memset(&k, 0, sizeof k);
    (void)fl_bytes_reader_read_str8(r, query, sizeof query);
    (void)fl_bytes_reader_read_bytes(r, &found, 1);
    (void)fl_bytes_reader_read_str8(r, k.key_id, sizeof k.key_id);
    (void)fl_bytes_reader_read_bytes(r, k.signing_public, 32);
    (void)fl_bytes_reader_read_bytes(r, k.box_public, 32);
    if (r->bad)
        return;
    k.found = found == 1u;
    for (size_t i = 0; i < KQ_KEYS_MAX && !slot; i++)
        if (s_keys[i].used && strcmp(s_keys[i].query, query) == 0)
            slot = &s_keys[i];
    for (size_t i = 0; i < KQ_KEYS_MAX && !slot; i++)
        if (!s_keys[i].used)
            slot = &s_keys[i];
    if (!slot)
        slot = &s_keys[0];
    slot->used = 1;
    snprintf(slot->query, sizeof slot->query, "%s", query);
    slot->keys = k;
}

void fl_net_kq_client_handle(fl_net_client_t *client, uint8_t opcode, const uint8_t *payload,
                             uint16_t plen) {
    fl_bytes_reader_t r;
    fl_bytes_reader_init(&r, payload, plen);

    (void)client;
    kq_lock();
    switch (opcode) {
    case FL_NET_SESSION_OP_KQ_ANNOUNCE:
        client_announce(&r);
        break;
    case FL_NET_SESSION_OP_KQ_RELEASE:
        client_release(&r);
        break;
    case FL_NET_SESSION_OP_KQ_PART:
        client_part(&r);
        break;
    case FL_NET_SESSION_OP_KQ_KEYS:
        client_keys(&r);
        break;
    default:
        break;
    }
    kq_unlock();
}

/* ---- this shell, sending ---------------------------------------------------- */

static const fl_net_server_member_t *host_self(fl_net_server_t *srv) {
    size_t count = fl_net_server_member_count(srv);
    for (size_t i = 0; i < count; i++) {
        const fl_net_server_member_t *m = fl_net_server_member_at(srv, i);
        if (m && m->is_host)
            return m;
    }
    return NULL;
}

fl_result_t fl_net_kq_send(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                           uint8_t opcode, const uint8_t *payload, uint16_t plen) {
    if (s_client_send) {
        s_client_send(client, opcode, payload, plen);
        return FL_RESULT_OK;
    }
    if (hosting) {
        const fl_net_server_member_t *self = srv ? host_self(srv) : NULL;
        if (!self)
            return FL_RESULT_NOENT;
        fl_net_kq_host_handle(srv, self, opcode, payload, plen);
        return FL_RESULT_OK;
    }
    if (!client)
        return FL_RESULT_INVAL;
    return fl_net_session_send_frame(client->peer_handle, opcode, payload, plen);
}

static void sleep_ms(unsigned ms) {
    struct timespec ts = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
}

fl_result_t fl_net_kq_lookup(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                             const char *who, unsigned wait_ms, fl_net_kq_keys_t *out) {
    uint8_t wbuf[FL_NET_SESSION_MAX_MSG];
    fl_bytes_sticky_writer_t w;
    fl_bytes_sticky_init(&w, wbuf, (uint16_t)sizeof wbuf);
    fl_result_t rc;

    if (!who || !out)
        return FL_RESULT_INVAL;
    kq_lock();
    for (size_t i = 0; i < KQ_KEYS_MAX; i++)
        if (s_keys[i].used && strcmp(s_keys[i].query, who) == 0)
            s_keys[i].used = 0; /* always ask afresh */
    kq_unlock();
    (void)fl_bytes_sticky_put_str8(&w, who);
    if (w.bad)
        return FL_RESULT_INVAL;
    rc = fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_KEYS, wbuf,
                        (uint16_t)w.w.len);
    if (rc != FL_RESULT_OK)
        return rc;
    for (unsigned waited = 0;; waited += 20u) {
        kq_lock();
        for (size_t i = 0; i < KQ_KEYS_MAX; i++) {
            if (s_keys[i].used && strcmp(s_keys[i].query, who) == 0) {
                *out = s_keys[i].keys;
                kq_unlock();
                return out->found ? FL_RESULT_OK : FL_RESULT_NOENT;
            }
        }
        kq_unlock();
        if (waited >= wait_ms)
            return FL_RESULT_TIMEDOUT;
        sleep_ms(20u);
    }
}

fl_result_t fl_net_kq_keys_cached(const char *who, fl_net_kq_keys_t *out) {
    fl_result_t rc = FL_RESULT_TIMEDOUT;

    if (!who || !out)
        return FL_RESULT_INVAL;
    kq_lock();
    for (size_t i = 0; i < KQ_KEYS_MAX; i++) {
        if (s_keys[i].used && strcmp(s_keys[i].query, who) == 0) {
            *out = s_keys[i].keys;
            rc = out->found ? FL_RESULT_OK : FL_RESULT_NOENT;
            break;
        }
    }
    kq_unlock();
    return rc;
}

fl_result_t fl_net_kq_send_delivery(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                    const char *sender_key, const char *recipient_key,
                                    const fl_kq_encoded_t *enc) {
    uint8_t wbuf[FL_NET_SESSION_MAX_MSG];
    fl_bytes_sticky_writer_t w;
    fl_bytes_sticky_init(&w, wbuf, (uint16_t)sizeof wbuf);
    fl_result_t rc;

    if (!sender_key || !recipient_key || !enc || !enc->letter.data || enc->letter.len == 0)
        return FL_RESULT_INVAL;
    (void)fl_bytes_sticky_put_bytes(&w, enc->delivery_id, FL_KQ_DELIVERY_ID_LEN);
    (void)fl_bytes_sticky_put_u32(&w, (uint32_t)enc->letter.len);
    (void)fl_bytes_sticky_put_str8(&w, sender_key);
    (void)fl_bytes_sticky_put_str8(&w, recipient_key);
    (void)fl_bytes_sticky_put_str8(&w, enc->file_name);
    kq_put_offer(&w, &enc->offer);
    if (w.bad)
        return FL_RESULT_INVAL; /* names too long for one session frame */
    rc = fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_OFFER, wbuf,
                        (uint16_t)w.w.len);
    for (size_t off = 0; rc == FL_RESULT_OK && off < enc->letter.len; off += KQ_PART_MAX) {
        size_t n = enc->letter.len - off < KQ_PART_MAX ? enc->letter.len - off : KQ_PART_MAX;
        uint8_t pbuf[FL_NET_SESSION_MAX_MSG];
        fl_bytes_sticky_writer_t p;
        fl_bytes_sticky_init(&p, pbuf, (uint16_t)sizeof pbuf);
        (void)fl_bytes_sticky_put_bytes(&p, enc->delivery_id, FL_KQ_DELIVERY_ID_LEN);
        (void)fl_bytes_sticky_put_u32(&p, (uint32_t)off);
        (void)fl_bytes_sticky_put_bytes(&p, enc->letter.data + off, n);
        if (p.bad)
            return FL_RESULT_INVAL;
        rc = fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_PART, pbuf,
                            (uint16_t)p.w.len);
    }
    return rc;
}

fl_result_t fl_net_kq_pull(fl_net_server_t *srv, fl_net_client_t *client, int hosting) {
    return fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_ANNOUNCE, NULL, 0u);
}

fl_result_t fl_net_kq_send_cosign(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                  const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const char *coauthor,
                                  const uint8_t sig[64]) {
    uint8_t wbuf[FL_NET_SESSION_MAX_MSG];
    fl_bytes_sticky_writer_t w;
    fl_bytes_sticky_init(&w, wbuf, (uint16_t)sizeof wbuf);

    (void)fl_bytes_sticky_put_bytes(&w, id, FL_KQ_DELIVERY_ID_LEN);
    (void)fl_bytes_sticky_put_str8(&w, coauthor);
    (void)fl_bytes_sticky_put_bytes(&w, sig, 64);
    if (w.bad)
        return FL_RESULT_INVAL;
    return fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_COSIGN, wbuf,
                          (uint16_t)w.w.len);
}

fl_result_t fl_net_kq_send_ack(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                               const uint8_t id[FL_KQ_DELIVERY_ID_LEN], int accepted) {
    uint8_t wbuf[FL_NET_SESSION_MAX_MSG];
    fl_bytes_sticky_writer_t w;
    fl_bytes_sticky_init(&w, wbuf, (uint16_t)sizeof wbuf);
    uint8_t a = accepted ? 1u : 0u;

    (void)fl_bytes_sticky_put_bytes(&w, id, FL_KQ_DELIVERY_ID_LEN);
    (void)fl_bytes_sticky_put_bytes(&w, &a, 1);
    if (w.bad)
        return FL_RESULT_INVAL;
    return fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_ACK, wbuf,
                          (uint16_t)w.w.len);
}

size_t fl_net_kq_host_sent(const char *sender, fl_kq_delivery_t *out, size_t cap) {
    size_t n = 0;

    if (!sender || !out || cap == 0)
        return 0;
    kq_lock();
    if (store())
        n = fl_kq_store_sent_by(store(), sender, out, cap);
    kq_unlock();
    return n;
}

size_t fl_net_kq_inbox(fl_net_kq_inbox_t *out, size_t cap) {
    size_t n = 0;
    kq_lock();
    for (size_t i = 0; i < KQ_INBOX_MAX && n < cap; i++)
        if (s_inbox[i].used)
            out[n++] = s_inbox[i].pub;
    kq_unlock();
    return n;
}

fl_result_t fl_net_kq_inbox_set_cosig(const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                                      const uint8_t sig[64]) {
    kq_inbox_slot_t *slot;
    fl_result_t rc = FL_RESULT_NOENT;

    kq_lock();
    slot = inbox_find(id);
    if (slot) {
        memcpy(slot->pub.sig, sig, sizeof slot->pub.sig);
        slot->pub.cosigned = 1;
        rc = FL_RESULT_OK;
    }
    kq_unlock();
    return rc;
}

fl_result_t fl_net_kq_take_letter(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], unsigned wait_ms,
                                  fl_kq_buf_t *letter) {
    for (unsigned waited = 0;; waited += 20u) {
        kq_inbox_slot_t *slot;

        kq_lock();
        slot = inbox_find(id);
        if (!slot) {
            kq_unlock();
            return FL_RESULT_NOENT;
        }
        if (slot->pub.letter_ready && slot->letter.data) {
            letter->data = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, slot->letter.len);
            if (!letter->data) {
                kq_unlock();
                return FL_RESULT_NOMEM;
            }
            memcpy(letter->data, slot->letter.data, slot->letter.len);
            letter->len = slot->letter.len;
            kq_unlock();
            return FL_RESULT_OK;
        }
        kq_unlock();
        if (waited >= wait_ms)
            return FL_RESULT_TIMEDOUT;
        sleep_ms(20u);
    }
}

void fl_net_kq_inbox_drop(const uint8_t id[FL_KQ_DELIVERY_ID_LEN]) {
    kq_inbox_slot_t *slot;
    kq_lock();
    slot = inbox_find(id);
    if (slot)
        inbox_free(slot);
    kq_unlock();
}

void fl_net_kq_reset(void) {
    kq_lock();
    for (size_t i = 0; i < KQ_UPLOADS_MAX; i++)
        if (s_uploads[i].used)
            upload_free(&s_uploads[i]);
    for (size_t i = 0; i < KQ_INBOX_MAX; i++)
        if (s_inbox[i].used)
            inbox_free(&s_inbox[i]);
    memset(s_keys, 0, sizeof s_keys);
    if (s_store) {
        fl_kq_store_close(s_store);
        s_store = NULL;
    }
    kq_unlock();
}
