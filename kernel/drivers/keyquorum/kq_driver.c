/*
 * KeyQuorum security driver (contracts/drivers/contract_p4_keyquorum.h, P4-9).
 *
 * Thin wrappers over keyquorum_files.h: each op asks KeyQuorum for the size it needs,
 * allocates, and calls again; it returns KeyQuorum's status unchanged. Secrets are wiped
 * through mem_domain_secure_zero (which extends mem_domain_zero) and digests compared by
 * asm_mem_ct_equal.
 */
#include "fl/driver/keyquorum.h"
#include "fl/mem_asm.h"
#include "mem_domain.h"

#include <stdlib.h>

/* The P4-9 contract copies these from keyquorum_files.h; a submodule bump that changes one
 * must fail the build, not the wire framing (the runtime probe checks only the linked ABI). */
_Static_assert(FL_KQ_ABI_VERSION == KQ_FILE_ABI_VERSION,
               "KeyQuorum header ABI differs from the P4-9 contract");
_Static_assert(FL_KQ_DELIVERY_ID_LEN == KQ_FILE_DELIVERY_ID_LEN,
               "delivery id length differs from KeyQuorum");
_Static_assert(sizeof(kq_file_offer_t) == FL_KQ_OFFER_WIRE_LEN,
               "kq_file_offer_t differs from the offer's wire length");

void fl_kq_buf_free(fl_kq_buf_t *buf) {
    if (!buf)
        return;
    if (buf->data) {
        mem_domain_secure_zero(buf->data, buf->len);
        free(buf->data);
    }
    buf->data = NULL;
    buf->len = 0;
}

/* After a KeyQuorum call that reports its size (first==KQ_FILE_NO_ROOM with need set),
 * allocates need bytes into *data and *cap. Any other first status is returned as is. out is
 * not touched here: it may alias an input of the second call. */
static int take_sized(int first, size_t need, uint8_t **data, size_t *cap) {
    if (first != KQ_FILE_NO_ROOM)
        return first;
    *data = (uint8_t *)malloc(need ? need : 1u);
    if (!*data)
        return KQ_FILE_INVALID;
    *cap = need;
    return KQ_FILE_NO_ROOM;
}

/*
 * Two-pass call: size, allocate, fill. out changes only after the second call succeeds, so an
 * out that is also an input stays intact during the call and on failure; on success the
 * buffer it held before (callers pass a zeroed or owned buffer) is wiped and freed.
 */
#define SIZED_CALL(out, call_with)                                        \
    do {                                                                  \
        size_t need_ = 0;                                                 \
        uint8_t *data_ = NULL;                                            \
        uint8_t *outp_ = NULL;                                            \
        size_t cap_ = 0;                                                  \
        int rc_ = (call_with);                                            \
        rc_ = take_sized(rc_, need_, &data_, &cap_);                      \
        if (rc_ != KQ_FILE_NO_ROOM)                                       \
            return rc_;                                                   \
        outp_ = data_;                                                    \
        rc_ = (call_with);                                                \
        if (rc_ != KQ_FILE_OK) {                                          \
            mem_domain_secure_zero(data_, cap_);                             \
            free(data_);                                                  \
            return rc_;                                                   \
        }                                                                 \
        fl_kq_buf_free(out);                                              \
        (out)->data = data_;                                              \
        (out)->len = need_;                                               \
        return KQ_FILE_OK;                                                \
    } while (0)

static int drv_file_new(const uint8_t file_id[16], const char *name, const char *scope_root,
                        fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_file_new(file_id, name, scope_root, outp_, cap_, &need_));
}

static int drv_check_in(const fl_kq_buf_t *c, const char *author, const char *created_at_utc,
                        const uint8_t *payload, size_t len, const uint8_t secret[32],
                        fl_kq_buf_t *out, uint8_t rev[32]) {
    SIZED_CALL(out, kq_file_check_in(c->data, c->len, author, created_at_utc, payload, len,
                                     secret, outp_, cap_, &need_, rev));
}

static int drv_verify(const fl_kq_buf_t *c, uint8_t root[32]) {
    return kq_file_verify(c->data, c->len, root);
}

static int drv_head(const fl_kq_buf_t *c, uint8_t rev[32]) {
    return kq_file_head(c->data, c->len, rev);
}

static int drv_payload(const fl_kq_buf_t *c, const uint8_t rev[32], fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_file_payload(c->data, c->len, rev, outp_, cap_, &need_));
}

static int drv_signers(const fl_kq_buf_t *c, fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_file_signers(c->data, c->len, outp_, cap_, &need_));
}

static int drv_merge(const fl_kq_buf_t *local, const fl_kq_buf_t *incoming, const char *actor,
                     const char *occurred_at_utc, fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_file_merge(local ? local->data : NULL, local ? local->len : 0u,
                                  incoming->data, incoming->len, actor, occurred_at_utc, outp_,
                                  cap_, &need_));
}

static int drv_select_read(const fl_kq_buf_t *c, const kq_file_trust_t *trust,
                           const uint8_t (*accepted)[32], size_t naccepted, uint8_t rev[32]) {
    return kq_file_select_read(c->data, c->len, trust, accepted, naccepted, rev);
}

static int drv_offer(const fl_kq_buf_t *c, const uint8_t rev[32], kq_file_offer_t *out) {
    return kq_file_offer(c->data, c->len, rev, out);
}

static int drv_seal(const fl_kq_buf_t *c, const uint8_t rev[32], const kq_file_trust_t *trust,
                    const char *sender, const uint8_t sender_secret[32],
                    const uint8_t sender_box_public[32], const char *recipient,
                    const uint8_t recipient_box_public[32], fl_kq_buf_t *letter,
                    uint8_t delivery_id[16]) {
    SIZED_CALL(letter, kq_file_seal_letter(c->data, c->len, rev, trust, sender, sender_secret,
                                           sender_box_public, recipient, recipient_box_public,
                                           outp_, cap_, &need_, delivery_id));
}

static int drv_offer_verify(const kq_file_offer_t *offer, const char *author,
                            const uint8_t author_public[32]) {
    return kq_file_offer_verify(offer, author, author_public);
}

static int drv_cosign(const kq_file_offer_t *offer, const char *coauthor,
                      const uint8_t secret[32], uint8_t sig[64]) {
    return kq_file_coauthor_sign(offer, coauthor, secret, sig);
}

static int drv_cosign_verify(const kq_file_offer_t *offer, const char *coauthor,
                             const uint8_t coauthor_public[32], const uint8_t sig[64]) {
    return kq_file_coauthor_verify(offer, coauthor, coauthor_public, sig);
}

static int drv_open(const fl_kq_buf_t *letter, const uint8_t box_secret[32], const char *sender,
                    const char *recipient, const uint8_t sender_public[32], fl_kq_buf_t *c,
                    uint8_t rev[32], uint8_t root[32]) {
    SIZED_CALL(c, kq_file_open_letter(letter->data, letter->len, box_secret, sender, recipient,
                                      sender_public, outp_, cap_, &need_, rev, root));
}

static int drv_attach(const fl_kq_buf_t *c, const kq_file_offer_t *offer, const char *coauthor,
                      const uint8_t sig[64], fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_file_attach_coauthor(c->data, c->len, offer, coauthor, sig, outp_, cap_,
                                            &need_));
}

static int drv_readiness(const fl_kq_buf_t *c, const uint8_t rev[32], const char *recipient,
                         const kq_file_trust_t *trust, uint32_t *have, uint32_t *need) {
    return kq_file_readiness(c->data, c->len, rev, recipient, trust, have, need);
}

static int drv_freshness(const uint8_t (*seen)[64], size_t nseen, const fl_kq_buf_t *c,
                         const uint8_t rev[32], const uint8_t root[32]) {
    return kq_file_freshness(seen, nseen, c->data, c->len, rev, root);
}

/* Lifecycle (the base driver's ops): the probe accepts this driver only when the linked
 * library speaks the ABI it was written for. There is no device to attach or start. */
static fl_result_t kq_probe(fl_device_t *dev) {
    (void)dev;
    return kq_file_abi_version() == FL_KQ_ABI_VERSION ? FL_RESULT_OK : FL_RESULT_PROBE_SKIP;
}

static int drv_permits_newer(const fl_kq_buf_t *c, const uint8_t rev[32],
                             const kq_file_trust_t *trust) {
    return kq_file_permits_newer(c->data, c->len, rev, trust);
}

static int drv_countersign(const fl_kq_buf_t *c, const uint8_t rev[32], const char *supervisor,
                           const uint8_t secret[32], const kq_file_trust_t *trust,
                           fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_file_countersign(c->data, c->len, rev, supervisor, secret, trust, outp_,
                                        cap_, &need_));
}

static int drv_events(const fl_kq_buf_t *c, fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_file_events(c->data, c->len, outp_, cap_, &need_));
}

static const fl_driver_ops_t s_kq_lifecycle = {
    .probe = kq_probe,
};

static const fl_kq_driver_t s_kq_driver = {
    .base = {
        .class = FL_DRV_CLASS_SECURITY,
        .name = "keyquorum",
        .ops = &s_kq_lifecycle,
        .synth_id = "keyquorum",
    },
    .file_new = drv_file_new,
    .check_in = drv_check_in,
    .verify = drv_verify,
    .head = drv_head,
    .payload = drv_payload,
    .signers = drv_signers,
    .merge = drv_merge,
    .select_read = drv_select_read,
    .offer = drv_offer,
    .seal = drv_seal,
    .offer_verify = drv_offer_verify,
    .cosign = drv_cosign,
    .cosign_verify = drv_cosign_verify,
    .open = drv_open,
    .attach = drv_attach,
    .permits_newer = drv_permits_newer,
    .countersign = drv_countersign,
    .events = drv_events,
    .readiness = drv_readiness,
    .freshness = drv_freshness,
    .wipe = mem_domain_secure_zero,
    .equal = asm_mem_ct_equal,
};

const fl_driver_desc_t *fl_kq_driver_desc(void) {
    return &s_kq_driver.base;
}

const fl_kq_driver_t *fl_kq_driver_get(void) {
    /* Through the base: the KeyQuorum ops are usable only when the lifecycle probe accepts. */
    return s_kq_driver.base.ops->probe(NULL) == FL_RESULT_OK ? &s_kq_driver : NULL;
}
