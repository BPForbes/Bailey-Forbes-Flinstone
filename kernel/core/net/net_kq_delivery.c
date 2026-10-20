#include "net_kq_delivery.h"
#include "contract_p3_session_wire.h"
#include "kq_checkpoint.h"
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
static fl_net_kq_users_fn s_users;
static char s_store_dir[1024];
static fl_kq_store_t *s_store;
static fl_net_kq_host_send_fn s_host_send;
static fl_net_kq_client_send_fn s_client_send;
static int64_t (*s_now)(void);

/* ---- wire codec ------------------------------------------------------------ */

typedef struct {
    uint8_t buf[FL_NET_SESSION_MAX_MSG];
    size_t  n;
    int     bad;
} kq_w_t;

typedef struct {
    const uint8_t *p;
    size_t         n;
    int            bad;
} kq_r_t;

static void w_bytes(kq_w_t *w, const void *src, size_t len) {
    if (w->bad || w->n + len > sizeof w->buf) {
        w->bad = 1;
        return;
    }
    memcpy(w->buf + w->n, src, len);
    w->n += len;
}

static void w_u32(kq_w_t *w, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    w_bytes(w, b, 4);
}

static void w_u64(kq_w_t *w, uint64_t v) {
    w_u32(w, (uint32_t)(v >> 32));
    w_u32(w, (uint32_t)v);
}

static void w_str(kq_w_t *w, const char *s) {
    size_t len = s ? strlen(s) : 0;
    uint8_t l = (uint8_t)len;
    if (len > 255u) {
        w->bad = 1;
        return;
    }
    w_bytes(w, &l, 1);
    w_bytes(w, s, len);
}

static void w_offer(kq_w_t *w, const kq_file_offer_t *o) {
    w_bytes(w, o->file_id, 16);
    w_bytes(w, o->revision_id, 32);
    w_bytes(w, o->content_commitment, 32);
    w_bytes(w, o->author_identity, 16);
    w_bytes(w, o->author_signature, 64);
    w_u64(w, o->topology_generation);
    w_bytes(w, o->policy_hash, 32);
}

static void r_bytes(kq_r_t *r, void *dst, size_t len) {
    if (r->bad || r->n < len) {
        r->bad = 1;
        return;
    }
    memcpy(dst, r->p, len);
    r->p += len;
    r->n -= len;
}

static uint32_t r_u32(kq_r_t *r) {
    uint8_t b[4] = {0};
    r_bytes(r, b, 4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

static uint64_t r_u64(kq_r_t *r) {
    uint64_t hi = r_u32(r);
    return (hi << 32) | r_u32(r);
}

static void r_str(kq_r_t *r, char *out, size_t cap) {
    uint8_t l = 0;
    r_bytes(r, &l, 1);
    if (r->bad || (size_t)l >= cap || r->n < l) {
        r->bad = 1;
        if (cap)
            out[0] = '\0';
        return;
    }
    memcpy(out, r->p, l);
    out[l] = '\0';
    r->p += l;
    r->n -= l;
}

static void r_offer(kq_r_t *r, kq_file_offer_t *o) {
    r_bytes(r, o->file_id, 16);
    r_bytes(r, o->revision_id, 32);
    r_bytes(r, o->content_commitment, 32);
    r_bytes(r, o->author_identity, 16);
    r_bytes(r, o->author_signature, 64);
    o->topology_generation = r_u64(r);
    r_bytes(r, o->policy_hash, 32);
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

static const fl_user_db_t *users(void) {
    return s_users ? s_users() : NULL;
}

/* The account a member signs in as, by the host's registry. */
static const char *key_of_member(const fl_net_server_member_t *m) {
    const fl_user_record_t *u = m ? fl_user_db_find(users(), m->principal) : NULL;
    return u ? u->key_id : NULL;
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

fl_result_t fl_net_kq_install(fl_net_kq_users_fn users_fn, const char *store_dir) {
    if (!users_fn || !store_dir || strlen(store_dir) >= sizeof s_store_dir)
        return FL_RESULT_INVAL;
    kq_lock();
    s_users = users_fn;
    snprintf(s_store_dir, sizeof s_store_dir, "%s", store_dir);
    kq_unlock();
    fl_net_server_set_kq_hook(fl_net_kq_host_handle);
    fl_net_client_set_kq_hook(fl_net_kq_client_handle);
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

typedef struct {
    int                       used;
    fl_net_server_member_id_t from;
    uint8_t                   id[FL_KQ_DELIVERY_ID_LEN];
    char                      author[FL_HERITAGE_KEY_ID_MAX];
    char                      recipient[FL_HERITAGE_KEY_ID_MAX];
    char                      file_name[128];
    kq_file_offer_t           offer;
    uint8_t                  *buf;
    size_t                    len;
    size_t                    got;
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
    kq_w_t w = {{0}, 0, 0};

    w_bytes(&w, d->id, FL_KQ_DELIVERY_ID_LEN);
    w_u64(&w, (uint64_t)d->expires_at);
    w_str(&w, d->sender);
    w_str(&w, d->file_name);
    w_offer(&w, &d->offer);
    if (w.bad)
        return;
    if (to)
        host_send(srv, to, (uint8_t)FL_NET_SESSION_OP_KQ_ANNOUNCE, w.buf, (uint16_t)w.n);
    else
        host_send_to_account(srv, d->recipient, (uint8_t)FL_NET_SESSION_OP_KQ_ANNOUNCE, w.buf,
                             (uint16_t)w.n);
}

/* RELEASE header then the letter in parts, to `to` or every member of the recipient. */
static void host_release(fl_net_server_t *srv, const fl_net_server_member_t *to,
                         const fl_kq_delivery_t *d) {
    uint8_t *letter = NULL;
    size_t len = 0;
    kq_w_t w = {{0}, 0, 0};

    if (fl_kq_store_letter(store(), d->id, &letter, &len) != FL_RESULT_OK)
        return;
    w_bytes(&w, d->id, FL_KQ_DELIVERY_ID_LEN);
    w_u32(&w, (uint32_t)len);
    if (to)
        host_send(srv, to, (uint8_t)FL_NET_SESSION_OP_KQ_RELEASE, w.buf, (uint16_t)w.n);
    else
        host_send_to_account(srv, d->recipient, (uint8_t)FL_NET_SESSION_OP_KQ_RELEASE, w.buf,
                             (uint16_t)w.n);
    for (size_t off = 0; off < len; off += KQ_PART_MAX) {
        size_t n = len - off < KQ_PART_MAX ? len - off : KQ_PART_MAX;
        kq_w_t p = {{0}, 0, 0};
        w_bytes(&p, d->id, FL_KQ_DELIVERY_ID_LEN);
        w_u32(&p, (uint32_t)off);
        w_bytes(&p, letter + off, n);
        if (to)
            host_send(srv, to, (uint8_t)FL_NET_SESSION_OP_KQ_PART, p.buf, (uint16_t)p.n);
        else
            host_send_to_account(srv, d->recipient, (uint8_t)FL_NET_SESSION_OP_KQ_PART, p.buf,
                                 (uint16_t)p.n);
    }
    free(letter);
}

static kq_upload_t *upload_find(fl_net_server_member_id_t from, const uint8_t *id) {
    for (size_t i = 0; i < KQ_UPLOADS_MAX; i++)
        if (s_uploads[i].used && s_uploads[i].from == from &&
            memcmp(s_uploads[i].id, id, FL_KQ_DELIVERY_ID_LEN) == 0)
            return &s_uploads[i];
    return NULL;
}

static void upload_free(kq_upload_t *u) {
    free(u->buf);
    memset(u, 0, sizeof *u);
}

/* Drops uploads idle past KQ_UPLOAD_IDLE_SEC on the host clock. */
static void upload_reclaim(int64_t now) {
    for (size_t i = 0; i < KQ_UPLOADS_MAX; i++)
        if (s_uploads[i].used && now - s_uploads[i].last_at > KQ_UPLOAD_IDLE_SEC)
            upload_free(&s_uploads[i]);
}

static void host_offer(fl_net_server_t *srv, const fl_net_server_member_t *from, kq_r_t *r) {
    kq_upload_t up;
    uint32_t len;
    const fl_user_record_t *recipient;
    const char *me = key_of_member(from);
    fl_kq_cp_result_t cp;
    fl_kq_delivery_t existing;

    memset(&up, 0, sizeof up);
    r_bytes(r, up.id, FL_KQ_DELIVERY_ID_LEN);
    len = r_u32(r);
    r_str(r, up.author, sizeof up.author);
    r_str(r, up.recipient, sizeof up.recipient);
    r_str(r, up.file_name, sizeof up.file_name);
    r_offer(r, &up.offer);
    if (r->bad || r->n != 0 || len == 0 || len > FL_KQ_LETTER_MAX) {
        host_refuse(srv, from, "delivery", "malformed offer");
        return;
    }
    /* A member offers only as the account it signed in as; the signature decides. */
    if (!me || strcmp(me, up.author) != 0) {
        host_refuse(srv, from, "delivery", "offer is not from this member's account");
        return;
    }
    recipient = fl_user_db_find_by_key(users(), up.recipient);
    if (!recipient || !fl_user_db_box_public(users(), up.recipient)) {
        host_refuse(srv, from, "delivery", fl_kq_cp_reason(FL_KQ_CP_NO_KEY));
        return;
    }
    cp = fl_kq_cp_admit_offer(users(), up.author, &up.offer);
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
        up.buf = (uint8_t *)malloc(len);
        if (!up.buf)
            break;
        up.used = 1;
        up.from = from->member_id;
        up.len = len;
        up.last_at = now_sec();
        s_uploads[i] = up;
        return;
    }
    host_refuse(srv, from, "delivery", "host is busy; try again");
}

static void host_part(fl_net_server_t *srv, const fl_net_server_member_t *from, kq_r_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint32_t off;
    kq_upload_t *u;
    fl_kq_delivery_t d;
    fl_result_t rc;

    r_bytes(r, id, sizeof id);
    off = r_u32(r);
    upload_reclaim(now_sec());
    u = r->bad ? NULL : upload_find(from->member_id, id);
    if (!u)
        return;
    u->last_at = now_sec();
    if (off != u->got || r->n == 0 || u->got + r->n > u->len) {
        upload_free(u);
        host_refuse(srv, from, "delivery", "letter parts out of order");
        return;
    }
    memcpy(u->buf + u->got, r->p, r->n);
    u->got += r->n;
    if (u->got < u->len)
        return;
    /* Complete: the latent space holds it, on the host's clock, and asks the recipient. */
    fl_kq_delivery_init(&d, u->id, u->author, u->recipient, u->file_name, &u->offer, now_sec(),
                        FL_KQ_DELIVERY_TTL_SEC);
    rc = store() ? fl_kq_store_put(store(), &d, u->buf, u->len) : FL_RESULT_ERR;
    upload_free(u);
    if (rc != FL_RESULT_OK) {
        host_refuse(srv, from, "delivery", "host could not hold the letter");
        return;
    }
    (void)fl_kq_store_update(store(), &d, FL_KQ_SPIKE_ANNOUNCE, d.recipient, d.offered_at);
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

static void host_cosign(fl_net_server_t *srv, const fl_net_server_member_t *from, kq_r_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    char coauthor[FL_HERITAGE_KEY_ID_MAX];
    uint8_t sig[64];
    fl_kq_delivery_t d;
    const char *me = key_of_member(from);
    fl_kq_cp_result_t cp;
    int64_t t = now_sec();

    r_bytes(r, id, sizeof id);
    r_str(r, coauthor, sizeof coauthor);
    r_bytes(r, sig, sizeof sig);
    if (r->bad || r->n != 0 || !store() || fl_kq_store_get(store(), id, &d) != FL_RESULT_OK) {
        host_refuse(srv, from, "co-signature", "no such delivery");
        return;
    }
    if (strcmp(coauthor, d.recipient) != 0 || !me || strcmp(me, d.recipient) != 0) {
        host_refuse(srv, from, "co-signature", fl_kq_cp_reason(FL_KQ_CP_NOT_RECIPIENT));
        return;
    }
    cp = fl_kq_cp_admit_cosign(users(), d.recipient, &d.offer, sig);
    if (cp != FL_KQ_CP_OK) {
        host_refuse(srv, from, "co-signature", fl_kq_cp_reason(cp));
        return;
    }
    switch (fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, t)) {
    case FL_KQ_NEURON_FIRE:
        if (fl_kq_store_update(store(), &d, FL_KQ_SPIKE_COSIGN, d.recipient, t) != FL_RESULT_OK ||
            fl_kq_store_update(store(), &d, FL_KQ_SPIKE_RELEASE, "host", t) != FL_RESULT_OK) {
            host_refuse(srv, from, "co-signature", "host could not record it");
            return;
        }
        host_release(srv, from, &d);
        return;
    case FL_KQ_NEURON_REFRACTORY:
        /* Already released: send it again to this member (only they can open it). */
        if (!d.acknowledged)
            host_release(srv, from, &d);
        return;
    case FL_KQ_NEURON_LEAKED:
        host_refuse(srv, from, "co-signature", "delivery expired");
        (void)fl_kq_store_leak(store(), t);
        return;
    case FL_KQ_NEURON_REST:
    default:
        (void)fl_kq_store_update(store(), &d, FL_KQ_SPIKE_COSIGN, d.recipient, t);
        return;
    }
}

static void host_ack(fl_net_server_t *srv, const fl_net_server_member_t *from, kq_r_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint8_t accepted = 0;
    fl_kq_delivery_t d;
    const char *me = key_of_member(from);

    (void)srv;
    r_bytes(r, id, sizeof id);
    r_bytes(r, &accepted, 1);
    if (r->bad || !store() || fl_kq_store_get(store(), id, &d) != FL_RESULT_OK || !me ||
        strcmp(me, d.recipient) != 0 || !d.fired)
        return;
    d.acknowledged = 1;
    if (fl_kq_store_update(store(), &d, FL_KQ_SPIKE_ACK, d.recipient, now_sec()) == FL_RESULT_OK)
        (void)fl_kq_store_forget_letter(store(), id);
}

static void host_keys(fl_net_server_t *srv, const fl_net_server_member_t *from, kq_r_t *r) {
    char who[FL_HERITAGE_KEY_ID_MAX];
    const fl_user_record_t *u;
    const unsigned char *sign = NULL;
    const unsigned char *box = NULL;
    uint8_t zero[32] = {0};
    uint8_t found;
    kq_w_t w = {{0}, 0, 0};

    r_str(r, who, sizeof who);
    if (r->bad)
        return;
    u = fl_user_db_find_by_key(users(), who);
    if (!u)
        u = fl_user_db_find(users(), who);
    if (u) {
        sign = fl_user_db_signing_public(users(), u->key_id);
        box = fl_user_db_box_public(users(), u->key_id);
    }
    found = (u && sign && box) ? 1u : 0u;
    w_str(&w, who);
    w_bytes(&w, &found, 1);
    w_str(&w, found ? u->key_id : "");
    w_bytes(&w, found ? sign : zero, 32);
    w_bytes(&w, found ? box : zero, 32);
    if (!w.bad)
        host_send(srv, from, (uint8_t)FL_NET_SESSION_OP_KQ_KEYS, w.buf, (uint16_t)w.n);
}

void fl_net_kq_host_handle(fl_net_server_t *srv, const fl_net_server_member_t *from,
                           uint8_t opcode, const uint8_t *payload, uint16_t plen) {
    kq_r_t r = {payload, plen, 0};

    if (!from || !users())
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

typedef struct {
    int               used;
    fl_net_kq_inbox_t pub;
    uint8_t          *letter;
    size_t            letter_len;
    size_t            letter_got;
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
    free(s->letter);
    memset(s, 0, sizeof *s);
}

static void client_announce(kq_r_t *r) {
    fl_net_kq_inbox_t in;
    kq_inbox_slot_t *slot;
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];

    memset(&in, 0, sizeof in);
    r_bytes(r, in.id, sizeof in.id);
    in.expires_at = (int64_t)r_u64(r);
    r_str(r, in.sender, sizeof in.sender);
    r_str(r, in.file_name, sizeof in.file_name);
    r_offer(r, &in.offer);
    if (r->bad || r->n != 0)
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

static void client_release(kq_r_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint32_t len;
    kq_inbox_slot_t *slot;

    r_bytes(r, id, sizeof id);
    len = r_u32(r);
    slot = r->bad ? NULL : inbox_find(id);
    if (!slot || len == 0 || len > FL_KQ_LETTER_MAX)
        return;
    free(slot->letter);
    slot->letter = (uint8_t *)malloc(len);
    slot->letter_len = slot->letter ? len : 0;
    slot->letter_got = 0;
    slot->pub.letter_ready = 0;
}

static void client_part(kq_r_t *r) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint32_t off;
    kq_inbox_slot_t *slot;

    r_bytes(r, id, sizeof id);
    off = r_u32(r);
    slot = r->bad ? NULL : inbox_find(id);
    if (!slot || !slot->letter || off != slot->letter_got ||
        slot->letter_got + r->n > slot->letter_len)
        return;
    memcpy(slot->letter + slot->letter_got, r->p, r->n);
    slot->letter_got += r->n;
    if (slot->letter_got == slot->letter_len)
        slot->pub.letter_ready = 1;
}

static void client_keys(kq_r_t *r) {
    char query[FL_HERITAGE_KEY_ID_MAX];
    fl_net_kq_keys_t k;
    uint8_t found = 0;
    kq_keys_slot_t *slot = NULL;

    memset(&k, 0, sizeof k);
    r_str(r, query, sizeof query);
    r_bytes(r, &found, 1);
    r_str(r, k.key_id, sizeof k.key_id);
    r_bytes(r, k.signing_public, 32);
    r_bytes(r, k.box_public, 32);
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
    kq_r_t r = {payload, plen, 0};

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
    kq_w_t w = {{0}, 0, 0};
    fl_result_t rc;

    if (!who || !out)
        return FL_RESULT_INVAL;
    kq_lock();
    for (size_t i = 0; i < KQ_KEYS_MAX; i++)
        if (s_keys[i].used && strcmp(s_keys[i].query, who) == 0)
            s_keys[i].used = 0; /* always ask afresh */
    kq_unlock();
    w_str(&w, who);
    if (w.bad)
        return FL_RESULT_INVAL;
    rc = fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_KEYS, w.buf,
                        (uint16_t)w.n);
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

fl_result_t fl_net_kq_send_delivery(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                    const char *sender_key, const char *recipient_key,
                                    const fl_kq_encoded_t *enc) {
    kq_w_t w = {{0}, 0, 0};
    fl_result_t rc;

    if (!sender_key || !recipient_key || !enc || !enc->letter.data || enc->letter.len == 0)
        return FL_RESULT_INVAL;
    w_bytes(&w, enc->delivery_id, FL_KQ_DELIVERY_ID_LEN);
    w_u32(&w, (uint32_t)enc->letter.len);
    w_str(&w, sender_key);
    w_str(&w, recipient_key);
    w_str(&w, enc->file_name);
    w_offer(&w, &enc->offer);
    if (w.bad)
        return FL_RESULT_INVAL; /* names too long for one session frame */
    rc = fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_OFFER, w.buf,
                        (uint16_t)w.n);
    for (size_t off = 0; rc == FL_RESULT_OK && off < enc->letter.len; off += KQ_PART_MAX) {
        size_t n = enc->letter.len - off < KQ_PART_MAX ? enc->letter.len - off : KQ_PART_MAX;
        kq_w_t p = {{0}, 0, 0};
        w_bytes(&p, enc->delivery_id, FL_KQ_DELIVERY_ID_LEN);
        w_u32(&p, (uint32_t)off);
        w_bytes(&p, enc->letter.data + off, n);
        rc = fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_PART, p.buf,
                            (uint16_t)p.n);
    }
    return rc;
}

fl_result_t fl_net_kq_pull(fl_net_server_t *srv, fl_net_client_t *client, int hosting) {
    return fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_ANNOUNCE, NULL, 0u);
}

fl_result_t fl_net_kq_send_cosign(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                  const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const char *coauthor,
                                  const uint8_t sig[64]) {
    kq_w_t w = {{0}, 0, 0};

    w_bytes(&w, id, FL_KQ_DELIVERY_ID_LEN);
    w_str(&w, coauthor);
    w_bytes(&w, sig, 64);
    if (w.bad)
        return FL_RESULT_INVAL;
    return fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_COSIGN, w.buf,
                          (uint16_t)w.n);
}

fl_result_t fl_net_kq_send_ack(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                               const uint8_t id[FL_KQ_DELIVERY_ID_LEN], int accepted) {
    kq_w_t w = {{0}, 0, 0};
    uint8_t a = accepted ? 1u : 0u;

    w_bytes(&w, id, FL_KQ_DELIVERY_ID_LEN);
    w_bytes(&w, &a, 1);
    return fl_net_kq_send(srv, client, hosting, (uint8_t)FL_NET_SESSION_OP_KQ_ACK, w.buf,
                          (uint16_t)w.n);
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
        if (slot->pub.letter_ready && slot->letter) {
            letter->data = (uint8_t *)malloc(slot->letter_len);
            if (!letter->data) {
                kq_unlock();
                return FL_RESULT_NOMEM;
            }
            memcpy(letter->data, slot->letter, slot->letter_len);
            letter->len = slot->letter_len;
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
