/*
 * KeyQuorum security driver (contracts/drivers/contract_p4_keyquorum.h, P4-9).
 *
 * Thin wrappers over keyquorum_files.h, keyquorum_identity.h (the account ops), keyquorum_slots.h,
 * keyquorum_access.h and keyquorum_bridge.h: each op asks KeyQuorum for the size it needs,
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
_Static_assert(FL_KQ_ACCOUNT_SECRET_LEN == KQ_ACCOUNT_SECRET_LEN, "account secret length differs");
_Static_assert(FL_KQ_ACCOUNT_PUBLIC_LEN == KQ_ACCOUNT_PUBLIC_LEN, "account public length differs");
_Static_assert(FL_KQ_ACCOUNT_SIGNATURE_LEN == KQ_ACCOUNT_SIGNATURE_LEN,
               "account signature length differs");
_Static_assert(FL_KQ_ACCOUNT_NONCE_LEN == KQ_ACCOUNT_NONCE_LEN, "account nonce length differs");
_Static_assert(FL_KQ_ACCOUNT_SEALED_LEN == KQ_ACCOUNT_SEALED_LEN, "account sealed length differs");
_Static_assert(FL_KQ_ACCOUNT_BRIDGE_REQUEST == KQ_ACCOUNT_BRIDGE_REQUEST,
               "bridge request role differs");
_Static_assert(FL_KQ_ACCOUNT_BRIDGE_ACCEPT == KQ_ACCOUNT_BRIDGE_ACCEPT,
               "bridge accept role differs");
_Static_assert(FL_KQ_ACCOUNT_OK == KQ_ACCOUNT_OK, "account OK differs");
_Static_assert(FL_KQ_ACCOUNT_INVALID == KQ_ACCOUNT_INVALID, "account INVALID differs");
_Static_assert(FL_KQ_ACCOUNT_NO_ROOM == KQ_ACCOUNT_NO_ROOM, "account NO_ROOM differs");
_Static_assert(FL_KQ_ACCOUNT_WRONG_PASSWORD == KQ_ACCOUNT_WRONG_PASSWORD,
               "account WRONG_PASSWORD differs");
_Static_assert(FL_KQ_ACCESS_NONE == KQ_ACCESS_NONE, "access NONE differs");
_Static_assert(FL_KQ_ACCESS_PASSWORD == KQ_ACCESS_PASSWORD, "access PASSWORD differs");
_Static_assert(FL_KQ_ACCESS_KQST_PASSWORD == KQ_ACCESS_KQST_PASSWORD, "access KQST_PASSWORD differs");
_Static_assert(FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST == KQ_ACCESS_PASSWORD_BRIDGE_KQST,
               "access PASSWORD_BRIDGE_KQST differs");
_Static_assert(FL_KQ_ACCESS_REFUSED == KQ_ACCESS_REFUSED, "access REFUSED differs");
_Static_assert(FL_KQ_ACCESS_INVALID == KQ_ACCESS_INVALID, "access INVALID differs");
_Static_assert(FL_KQ_BRIDGE_OK == KQ_BRIDGE_OK, "bridge OK differs");
_Static_assert(FL_KQ_BRIDGE_INVALID == KQ_BRIDGE_INVALID, "bridge INVALID differs");
_Static_assert(FL_KQ_BRIDGE_NO_ROOM == KQ_BRIDGE_NO_ROOM, "bridge NO_ROOM differs");
_Static_assert(FL_KQ_BRIDGE_REFUSED == KQ_BRIDGE_REFUSED, "bridge REFUSED differs");
_Static_assert(FL_KQ_BRIDGE_STALE == KQ_BRIDGE_STALE, "bridge STALE differs");
_Static_assert(sizeof(kq_file_offer_t) == FL_KQ_OFFER_WIRE_LEN,
               "kq_file_offer_t differs from the offer's wire length");

void fl_kq_buf_free(fl_kq_buf_t *buf) {
    if (!buf)
        return;
    if (buf->data) {
        mem_domain_secure_zero(buf->data, buf->len);
        mem_domain_free(MEM_DOMAIN_FS, buf->data);
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
    *data = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, need ? need : 1u);
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
            mem_domain_free(MEM_DOMAIN_FS, data_);                        \
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

/* Account keys: one-to-one over kq_account_*, status unchanged, no buffer kept. */
static int drv_account_keygen(uint8_t secret[32], uint8_t pub[32]) {
    return kq_account_keygen(secret, pub);
}

static int drv_account_public(const uint8_t secret[32], uint8_t pub[32]) {
    return kq_account_public(secret, pub);
}

static int drv_account_box_keygen(uint8_t secret[32], uint8_t pub[32]) {
    return kq_account_box_keygen(secret, pub);
}

static int drv_account_box_public(const uint8_t secret[32], uint8_t pub[32]) {
    return kq_account_box_public(secret, pub);
}

static int drv_account_seal(const char *password, const uint8_t secret[32], uint8_t *out,
                            size_t cap, size_t *out_len) {
    return kq_account_seal(password, secret, out, cap, out_len);
}

static int drv_account_open(const char *password, const uint8_t *sealed, size_t len,
                            uint8_t secret[32]) {
    return kq_account_open(password, sealed, len, secret);
}

static int drv_account_bridge_sign(int role, const char *from, const char *to,
                                   const char *signer, const uint8_t nonce[16],
                                   const uint8_t secret[32], uint8_t sig[64]) {
    return kq_account_bridge_sign(role, from, to, signer, nonce, secret, sig);
}

static int drv_account_bridge_verify(int role, const char *from, const char *to,
                                     const char *signer, const uint8_t nonce[16],
                                     const uint8_t pub[32], const uint8_t sig[64]) {
    return kq_account_bridge_verify(role, from, to, signer, nonce, pub, sig);
}

static const char *drv_event_name(uint8_t code) {
    return kq_file_event_name(code);
}

static const char *drv_outcome_name(uint8_t code) {
    return kq_file_outcome_name(code);
}

static int drv_seal_snapshot(const fl_kq_buf_t *c, uint32_t upto_events, const char *sender,
                             const uint8_t sender_secret[32], const uint8_t sender_box_public[32],
                             const char *recipient, const uint8_t recipient_box_public[32],
                             fl_kq_buf_t *letter, uint8_t delivery_id[16]) {
    SIZED_CALL(letter, kq_file_seal_snapshot(c->data, c->len, upto_events, sender, sender_secret,
                                             sender_box_public, recipient, recipient_box_public,
                                             outp_, cap_, &need_, delivery_id));
}

static int drv_open_snapshot(const fl_kq_buf_t *letter, const uint8_t box_secret[32],
                             const char *expected_sender, const char *expected_recipient,
                             const uint8_t sender_public[32], fl_kq_buf_t *snap, uint8_t root[32],
                             uint32_t *event_count) {
    SIZED_CALL(snap, kq_file_open_snapshot(letter->data, letter->len, box_secret, expected_sender,
                                           expected_recipient, sender_public, outp_, cap_, &need_,
                                           root, event_count));
}

static int drv_snapshot_covers(const fl_kq_buf_t *extract, const fl_kq_buf_t *snapshot) {
    return kq_file_snapshot_covers(extract->data, extract->len, snapshot->data, snapshot->len);
}

static int drv_error_sign(uint16_t code, uint8_t class_code, uint8_t step,
                          const uint8_t object_ref[32], const char *actor_label,
                          const char *occurred_at, const char *detail_key, const char *detail_value,
                          const uint8_t signing_secret[32], fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_error_sign(code, class_code, step, object_ref, actor_label, occurred_at,
                                  detail_key, detail_value, signing_secret, outp_, cap_, &need_));
}

static int drv_error_inject(const fl_kq_buf_t *kqtf, const fl_kq_buf_t *kqer,
                            const uint8_t reporter_public[32], const uint8_t pkg_sha256[32],
                            fl_kq_buf_t *out) {
    SIZED_CALL(out, kq_error_inject(kqtf->data, kqtf->len, kqer->data, kqer->len, reporter_public,
                                    pkg_sha256, outp_, cap_, &need_));
}

static const char *drv_error_name(uint16_t code) {
    return kq_error_name(code);
}

static const char *drv_error_class_name(uint8_t class_code) {
    return kq_error_class_name(class_code);
}

static int drv_slot_open(const uint8_t *kqst, size_t klen, const char *label, const char *passphrase,
                         const uint8_t expected_device_id[16], const uint8_t expected_signing_pub[32],
                         const uint8_t expected_encryption_pub[32], uint8_t signing_secret_out[32],
                         uint8_t encryption_secret_out[32]) {
    return kq_slot_open(kqst, klen, label, passphrase, expected_device_id, expected_signing_pub,
                        expected_encryption_pub, signing_secret_out, encryption_secret_out);
}

/* KeyQuorum writes the label without a terminator; one byte is kept back for it. */
static int drv_head_author(const fl_kq_buf_t *c, char *out, size_t cap) {
    size_t n = 0;
    int rc;

    if (!out || cap == 0)
        return KQ_FILE_NO_ROOM;
    out[0] = '\0';
    if (!c || !c->data)
        return KQ_FILE_INVALID;
    rc = kq_file_head_author(c->data, c->len, (unsigned char *)out, cap - 1u, &n);
    if (rc == KQ_FILE_OK && n >= cap)
        rc = KQ_FILE_NO_ROOM;
    if (rc != KQ_FILE_OK) {
        out[0] = '\0';
        return rc;
    }
    out[n] = '\0';
    return KQ_FILE_OK;
}

#ifndef FL_FREESTANDING
/* The freestanding image has no account store, slot token or bridge to ask about, and the SECFEAT
 * image cannot carry these three (about 13 KiB of library code past the 0x9f000 budget): there the
 * ops are NULL and every caller fails closed. */
static int drv_access_requirement(const char *current, const char *target, int sudo,
                                  const kq_bridge_t *live_bridges, size_t nbridges,
                                  int parent_approval, int *bridge_index_out,
                                  int *needs_parent_grant_out) {
    return kq_access_requirement(current, target, sudo, live_bridges, nbridges, parent_approval,
                                 bridge_index_out, needs_parent_grant_out);
}

static int drv_bridge_prove(const char *uid, uint32_t generation, const uint8_t salt[16],
                            const uint8_t bridge_public[32], const char *local_label,
                            const uint8_t *sealed_bridge_key, size_t slen,
                            const uint8_t wrap_salt[16], const uint8_t encryption_sk[32],
                            const uint8_t signing_sk[32], const uint8_t *challenge, size_t clen,
                            uint8_t *artifact_out, size_t cap, size_t *artifact_len) {
    return kq_bridge_prove(uid, generation, salt, bridge_public, local_label, sealed_bridge_key,
                           slen, wrap_salt, encryption_sk, signing_sk, challenge, clen,
                           artifact_out, cap, artifact_len);
}

static int drv_bridge_verify(const char *uid, uint32_t generation, const uint8_t salt[16],
                             const uint8_t bridge_public[32], int destroyed,
                             const char *const *member_labels, const uint8_t *member_signing_pubs,
                             size_t nmembers, const char *verifier_label, const uint8_t *challenge,
                             size_t clen, const uint8_t *artifact, size_t alen) {
    return kq_bridge_verify(uid, generation, salt, bridge_public, destroyed, member_labels,
                            member_signing_pubs, nmembers, verifier_label, challenge, clen,
                            artifact, alen);
}
#endif

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
    .account_keygen = drv_account_keygen,
    .account_public = drv_account_public,
    .account_box_keygen = drv_account_box_keygen,
    .account_box_public = drv_account_box_public,
    .account_seal = drv_account_seal,
    .account_open = drv_account_open,
    .account_bridge_sign = drv_account_bridge_sign,
    .account_bridge_verify = drv_account_bridge_verify,
    .event_name = drv_event_name,
    .outcome_name = drv_outcome_name,
    .seal_snapshot = drv_seal_snapshot,
    .open_snapshot = drv_open_snapshot,
    .snapshot_covers = drv_snapshot_covers,
    .error_sign = drv_error_sign,
    .error_inject = drv_error_inject,
    .error_name = drv_error_name,
    .error_class_name = drv_error_class_name,
    .slot_open = drv_slot_open,
    .head_author = drv_head_author,
#ifndef FL_FREESTANDING
    .access_requirement = drv_access_requirement,
    .bridge_prove = drv_bridge_prove,
    .bridge_verify = drv_bridge_verify,
#endif
};

const fl_driver_desc_t *fl_kq_driver_desc(void) {
    return &s_kq_driver.base;
}

#ifdef FL_FREESTANDING
/* The freestanding image has no driver model or registry: the resolver is the same ABI probe, on
 * the one static descriptor. NULL when the linked library speaks another ABI. */
const fl_kq_driver_t *fl_kq_driver_get(void) {
    return kq_probe(NULL) == FL_RESULT_OK ? &s_kq_driver : NULL;
}

const fl_kq_driver_t *fl_kq_driver_get_or_register(void) {
    return fl_kq_driver_get();
}
#else

const fl_kq_driver_t *fl_kq_driver_get_or_register(void) {
    /* Before drivers_init (session seed, unit tests, the browser lab) the descriptor may not
     * be registered yet; after drivers_shutdown ensure refuses and this stays NULL. */
    (void)fl_driver_registry_ensure(&s_kq_driver.base);
    return fl_kq_driver_get();
}
#endif
