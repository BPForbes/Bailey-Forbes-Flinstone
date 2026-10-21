/*
 * The native shell's account store as a KeyQuorum key directory, and the checkpoints over
 * it. Each form here only wraps its `_with` counterpart in kq_checkpoint.c; the browser lab
 * builds kq_checkpoint.c without this file and passes its own directory.
 */
#include "kq_checkpoint.h"
#include "user_db.h"

#include <stddef.h>

static const fl_user_db_t *db_of(const fl_kq_keydir_t *dir) {
    return ((const fl_user_db_keydir_t *)dir)->db;
}

/* A key id first, then an account name. They cannot collide: a name is one segment and
 * never "Root" (heritage::name_available), a key id is "Root" or dotted. */
static const char *resolve(const fl_kq_keydir_t *dir, const char *who) {
    const fl_user_record_t *u = fl_user_db_find_by_key(db_of(dir), who);

    if (!u)
        u = fl_user_db_find(db_of(dir), who);
    return u ? u->key_id : NULL;
}

static const unsigned char *signing_public(const fl_kq_keydir_t *dir, const char *label) {
    return fl_user_db_signing_public(db_of(dir), label);
}

static const unsigned char *box_public(const fl_kq_keydir_t *dir, const char *label) {
    return fl_user_db_box_public(db_of(dir), label);
}

void fl_user_db_keydir_init(fl_user_db_keydir_t *dir, const fl_user_db_t *db) {
    dir->base.resolve = resolve;
    dir->base.signing_public = signing_public;
    dir->base.box_public = box_public;
    dir->db = db;
}

/* NULL users stays NULL keys, which every checkpoint refuses as INVALID. */
#define KEYS(users, dir) ((users) ? (fl_user_db_keydir_init(&(dir), (users)), &(dir).base) : NULL)

fl_kq_cp_result_t fl_kq_cp_encode(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_secret[32],
                                  const char *recipient_key,
                                  const uint8_t recipient_box_public[32], const char *path,
                                  const char *created_at_utc, fl_kq_encoded_t *out) {
    fl_user_db_keydir_t dir;

    return fl_kq_cp_encode_with(KEYS(users, dir), sender_key, sender_signing_secret,
                                recipient_key, recipient_box_public, path, created_at_utc, out);
}

fl_kq_cp_result_t fl_kq_cp_admit_offer(const fl_user_db_t *users, const char *author_key,
                                       const kq_file_offer_t *offer) {
    fl_user_db_keydir_t dir;

    return fl_kq_cp_admit_offer_with(KEYS(users, dir), author_key, offer);
}

fl_kq_cp_result_t fl_kq_cp_admit_cosign(const fl_user_db_t *users, const char *recipient_key,
                                        const kq_file_offer_t *offer, const uint8_t sig[64]) {
    fl_user_db_keydir_t dir;

    return fl_kq_cp_admit_cosign_with(KEYS(users, dir), recipient_key, offer, sig);
}

fl_kq_cp_result_t fl_kq_cp_decode(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_public[32], const char *me_key,
                                  const uint8_t box_secret[32], const fl_kq_buf_t *letter,
                                  const kq_file_offer_t *offer, const uint8_t my_sig[64],
                                  const char *dest_path) {
    fl_user_db_keydir_t dir;

    return fl_kq_cp_decode_with(KEYS(users, dir), sender_key, sender_signing_public, me_key,
                                box_secret, letter, offer, my_sig, dest_path);
}

fl_kq_cp_result_t fl_kq_cp_permit(const fl_user_db_t *users, const char *supervisor_key,
                                  const uint8_t supervisor_secret[32], const char *path,
                                  const uint8_t rev[32]) {
    fl_user_db_keydir_t dir;

    return fl_kq_cp_permit_with(KEYS(users, dir), supervisor_key, supervisor_secret, path, rev);
}

fl_kq_cp_result_t fl_kq_cp_reseal(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_secret[32],
                                  const char *recipient_key,
                                  const uint8_t recipient_box_public[32], const char *path,
                                  const uint8_t rev[32], fl_kq_encoded_t *out) {
    fl_user_db_keydir_t dir;

    return fl_kq_cp_reseal_with(KEYS(users, dir), sender_key, sender_signing_secret,
                                recipient_key, recipient_box_public, path, rev, out);
}

