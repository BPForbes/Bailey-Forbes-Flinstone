#ifndef FL_KQ_CHECKPOINT_H
#define FL_KQ_CHECKPOINT_H


/* Secrets stay the caller's to wipe; every buffer holding content is wiped before free. */
#include "contract_result.h"
#include "fl/driver/keyquorum.h"
#include "kq_keydir.h"
#include "kq_storage.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FL_KQ_CP_OK = 0,
    FL_KQ_CP_NO_DRIVER,
    FL_KQ_CP_NO_KEY,
    FL_KQ_CP_BAD_SIGNATURE,
    FL_KQ_CP_NOT_RECIPIENT,
    FL_KQ_CP_UNTRUSTED,
    FL_KQ_CP_FORKED,
    FL_KQ_CP_BELOW_THRESHOLD,
    FL_KQ_CP_REPLAYED,
    FL_KQ_CP_NOT_NEWER,
    FL_KQ_CP_EXISTS,
    FL_KQ_CP_TOO_LARGE,
    FL_KQ_CP_IO,
    FL_KQ_CP_INVALID,
    FL_KQ_CP_NEEDS_EDITOR_PERMIT,
} fl_kq_cp_result_t;

const char *fl_kq_cp_reason(fl_kq_cp_result_t r);
fl_result_t fl_kq_cp_to_result(fl_kq_cp_result_t r);

int fl_kq_cp_safe_name(const char *name);

typedef struct {
    kq_file_offer_t offer;
    fl_kq_buf_t     letter;
    uint8_t         delivery_id[FL_KQ_DELIVERY_ID_LEN];
    char            file_name[128];
} fl_kq_encoded_t;

void fl_kq_encoded_free(fl_kq_encoded_t *enc);

#define FL_KQ_CONTAINER_SUFFIX ".kqtf"
#define FL_KQ_SEEN_SUFFIX ".kqseen"

fl_kq_cp_result_t fl_kq_cp_encode_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const char *created_at_utc, fl_kq_encoded_t *out);

fl_kq_cp_result_t fl_kq_cp_admit_offer_with(const fl_kq_keydir_t *keys, const char *author_key,
                                            const kq_file_offer_t *offer);

fl_kq_cp_result_t fl_kq_cp_admit_cosign_with(const fl_kq_keydir_t *keys,
                                             const char *recipient_key,
                                             const kq_file_offer_t *offer, const uint8_t sig[64]);

fl_kq_cp_result_t fl_kq_cp_cosign(const kq_file_offer_t *offer, const char *me_key,
                                  const uint8_t signing_secret[32], uint8_t sig[64]);

fl_kq_cp_result_t fl_kq_cp_reseal_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const uint8_t rev[32], fl_kq_encoded_t *out);

fl_kq_cp_result_t fl_kq_cp_history(const char *path, fl_kq_buf_t *lines);

fl_kq_cp_result_t fl_kq_cp_head(const char *path, uint8_t rev[32]);

/* FORKED with two heads; INVALID for no revision or a label over cap - 1. */
fl_kq_cp_result_t fl_kq_cp_head_author(const char *path, char *out, size_t cap);

fl_kq_cp_result_t fl_kq_cp_last_signature(const char *path, int64_t *sec_out);

/* Writes nothing; caller frees l15 and l17 (fl_kq_buf_free). */
fl_kq_cp_result_t fl_kq_cp_history_letters_with(const fl_kq_keydir_t *keys, const char *host_key,
                                                const uint8_t host_signing_secret[32], const char *requester_key,
                                                const char *path, fl_kq_buf_t *l15, fl_kq_buf_t *l17);

/* Starts path's history with a revision signed by author_key (the store's key for it must exist):
 * EXISTS when one is already kept beside it; rev_out (optional) is the new revision. */
fl_kq_cp_result_t fl_kq_cp_track_with(const fl_kq_keydir_t *keys, const char *path, const char *author_key,
                                      const uint8_t signing_secret[32], const char *created_at_utc,
                                      uint8_t rev_out[32]);

/* Whether the single head is trusted under the file's own policy and the keys the store holds for its
 * signers: OK with rev = the head, UNTRUSTED, FORKED, IO without a history, INVALID when damaged. */
fl_kq_cp_result_t fl_kq_cp_head_trusted_with(const fl_kq_keydir_t *keys, const char *path, uint8_t rev[32]);

fl_kq_cp_result_t fl_kq_cp_sign_head_with(const char *path, const char *author_key,
                                          const uint8_t signing_secret[32], const char *created_at_utc);

const char *fl_kq_cp_event_name(uint8_t code);
const char *fl_kq_cp_outcome_name(uint8_t code);

size_t fl_kq_cp_format_event(const char *line, size_t n, char *out, size_t cap);

/* Valid only during the call; line is not NUL-terminated, so use n. */
typedef int (*fl_kq_cp_event_fn)(const char *line, size_t n, void *ctx);

size_t fl_kq_cp_for_each_event(const fl_kq_buf_t *lines, fl_kq_cp_event_fn fn, void *ctx);

fl_kq_cp_result_t fl_kq_cp_permit_with(const fl_kq_keydir_t *keys, const char *supervisor_key,
                                       const uint8_t supervisor_secret[32], const char *path,
                                       const uint8_t rev[32]);

/* A file at dest_path with no sidecar is never overwritten (EXISTS). */
fl_kq_cp_result_t fl_kq_cp_decode_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_public[32],
                                       const char *me_key, const uint8_t box_secret[32],
                                       const fl_kq_buf_t *letter, const kq_file_offer_t *offer,
                                       const uint8_t my_sig[64], const char *dest_path);

fl_kq_cp_result_t fl_kq_cp_decode_in_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                          const uint8_t sender_signing_public[32],
                                          const char *me_key, const uint8_t box_secret[32],
                                          const fl_kq_buf_t *letter, const kq_file_offer_t *offer,
                                          const uint8_t my_sig[64], const fl_kq_dir_t *dir,
                                          const char *leaf);

#define FL_KQ_REPORT_SUFFIX ".kqer"

void fl_kq_cp_offer_encode(const kq_file_offer_t *o, uint8_t out[FL_KQ_OFFER_WIRE_LEN]);
void fl_kq_cp_offer_decode(const uint8_t in[FL_KQ_OFFER_WIRE_LEN], kq_file_offer_t *o);

#define FL_KQ_REPORT_MAX 1024u

#define FL_KQ_REPORT_CAP 64u
#define FL_KQ_REPORT_LEDGER "reports" FL_KQ_REPORT_SUFFIX

int fl_kq_cp_report_path(const char *dir, const uint8_t ref[32], char *out, size_t cap);

/* Returns OK, EXISTS, TOO_LARGE, INVALID or IO. */
fl_kq_cp_result_t fl_kq_cp_report_write_in(uint16_t code, uint8_t class_code, uint8_t step,
                                           const uint8_t ref[32], const char *actor,
                                           const char *occurred_at, const uint8_t secret[32],
                                           const fl_kq_dir_t *dir);

/* BAD_SIGNATURE leaves out empty; caller frees out. */
fl_kq_cp_result_t fl_kq_cp_report_read_in(const fl_kq_dir_t *dir, const uint8_t ref[32],
                                          const uint8_t reporter_public[32], fl_kq_buf_t *out);

fl_kq_cp_result_t fl_kq_cp_inject_report_bytes(const uint8_t reporter_public[32], const uint8_t *kqer,
                                               size_t kqer_len, const uint8_t ref[32], const char *path);

fl_kq_cp_result_t fl_kq_cp_inject_report_with(const fl_kq_keydir_t *keys, const char *reporter_key,
                                              const char *report_path, const uint8_t ref[32],
                                              const char *path);

fl_result_t fl_kq_cp_offer_ref(const kq_file_offer_t *o, uint8_t ref[32]);


typedef struct fl_user_db fl_user_db_t;

typedef struct {
    fl_kq_keydir_t      base;
    const fl_user_db_t *db;
} fl_user_db_keydir_t;

void fl_user_db_keydir_init(fl_user_db_keydir_t *dir, const fl_user_db_t *db);

fl_kq_cp_result_t fl_kq_cp_encode(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_secret[32],
                                  const char *recipient_key,
                                  const uint8_t recipient_box_public[32], const char *path,
                                  const char *created_at_utc, fl_kq_encoded_t *out);

fl_kq_cp_result_t fl_kq_cp_reseal(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_secret[32],
                                  const char *recipient_key,
                                  const uint8_t recipient_box_public[32], const char *path,
                                  const uint8_t rev[32], fl_kq_encoded_t *out);
fl_kq_cp_result_t fl_kq_cp_permit(const fl_user_db_t *users, const char *supervisor_key,
                                  const uint8_t supervisor_secret[32], const char *path,
                                  const uint8_t rev[32]);
fl_kq_cp_result_t fl_kq_cp_admit_offer(const fl_user_db_t *users, const char *author_key,
                                       const kq_file_offer_t *offer);
fl_kq_cp_result_t fl_kq_cp_admit_cosign(const fl_user_db_t *users, const char *recipient_key,
                                        const kq_file_offer_t *offer, const uint8_t sig[64]);
fl_kq_cp_result_t fl_kq_cp_decode(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_public[32], const char *me_key,
                                  const uint8_t box_secret[32], const fl_kq_buf_t *letter,
                                  const kq_file_offer_t *offer, const uint8_t my_sig[64],
                                  const char *dest_path);
fl_kq_cp_result_t fl_kq_cp_decode_in(const fl_user_db_t *users, const char *sender_key,
                                     const uint8_t sender_signing_public[32], const char *me_key,
                                     const uint8_t box_secret[32], const fl_kq_buf_t *letter,
                                     const kq_file_offer_t *offer, const uint8_t my_sig[64],
                                     const fl_kq_dir_t *dir, const char *leaf);

#endif /* FL_KQ_CHECKPOINT_H */
