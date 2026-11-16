#ifndef FL_KQ_CHECKPOINT_H
#define FL_KQ_CHECKPOINT_H

/*
 * KeyQuorum security checkpoints (contracts/drivers/contract_p4_keyquorum.h).
 *
 * The kernel orchestrator for co-signed delivery: it decides which checkpoint runs and
 * what an outcome means, and calls only the KeyQuorum driver (fl/driver/keyquorum.h) and
 * an account store for public keys (kq_keydir.h). No network here; net_kq_delivery.c
 * carries the results between parties.
 *
 *   encode   (sender)    check in + sign, public offer, letter sealed to the recipient
 *   admit    (host)      sender's signature on the offer; recipient's co-signature
 *   cosign   (recipient) co-sign the public offer
 *   decode   (recipient) open, attach own co-signature, full readiness, freshness, write
 *
 * Secrets are passed in by the caller (opened from user_db with the account password)
 * and stay the caller's to wipe; every buffer that held content is wiped before free.
 *
 * Each checkpoint takes the key directory it reads (the `_with` form). The native shell's
 * forms without the suffix take its user store and wrap it (kq_checkpoint_users.c); the
 * browser lab passes its own directory, so the same checkpoints run there.
 */

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

/**
 * Returns nonzero when name is a plain file name a delivery may use: fl_fs_name_is_plain and not a
 * reserved sidecar suffix (fl_server_shared_name_is_reserved). Every name from the wire is checked
 * with it before it is stored, printed or joined to a directory.
 */
int fl_kq_cp_safe_name(const char *name);

/** What the sender's encoder produces for one delivery. */
typedef struct {
    kq_file_offer_t offer;
    fl_kq_buf_t     letter;
    uint8_t         delivery_id[FL_KQ_DELIVERY_ID_LEN];
    char            file_name[128];
} fl_kq_encoded_t;

void fl_kq_encoded_free(fl_kq_encoded_t *enc);

/** Sidecars beside a tracked file: its container and the (revision, root) pairs accepted. */
#define FL_KQ_CONTAINER_SUFFIX ".kqtf"
#define FL_KQ_SEEN_SUFFIX ".kqseen"

/**
 * Encode checkpoint (sender). Checks in the file at path as a new revision by sender_key
 * on the file's current head (its container is path + FL_KQ_CONTAINER_SUFFIX, created on
 * first use with sender_key as owner), signs it with sender_signing_secret, and seals it
 * to recipient_box_public. FORKED when the file has two heads.
 */
fl_kq_cp_result_t fl_kq_cp_encode_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const char *created_at_utc, fl_kq_encoded_t *out);

/** Latent checkpoint (host): the offer carries author_key's signature under the host registry. */
fl_kq_cp_result_t fl_kq_cp_admit_offer_with(const fl_kq_keydir_t *keys, const char *author_key,
                                            const kq_file_offer_t *offer);

/** Latent checkpoint (host): sig is recipient_key's co-signature on the offer. */
fl_kq_cp_result_t fl_kq_cp_admit_cosign_with(const fl_kq_keydir_t *keys,
                                             const char *recipient_key,
                                             const kq_file_offer_t *offer, const uint8_t sig[64]);

/** Co-sign checkpoint (recipient). */
fl_kq_cp_result_t fl_kq_cp_cosign(const kq_file_offer_t *offer, const char *me_key,
                                  const uint8_t signing_secret[32], uint8_t sig[64]);

/**
 * Reseal (sender): the sealed letter for revision rev already in the container beside path.
 * Checks nothing in and writes nothing; a newer copy goes out this way once its supervisor has
 * countersigned the revision (fl_kq_cp_permit).
 */
fl_kq_cp_result_t fl_kq_cp_reseal_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const uint8_t rev[32], fl_kq_encoded_t *out);

/**
 * History (offline): the events of the file at path (its container beside it), one
 * tab-separated line each (see keyquorum_files.h). Needs only the local container.
 */
fl_kq_cp_result_t fl_kq_cp_history(const char *path, fl_kq_buf_t *lines);

/** The file's current head revision (its container beside path). FORKED with two heads. */
fl_kq_cp_result_t fl_kq_cp_head(const char *path, uint8_t rev[32]);

/**
 * Writes the author label of the file's single head revision to out ("" on every result but OK).
 * Returns FORKED with two heads, IO when the container cannot be read, INVALID for no revision, a
 * label longer than cap - 1 or a malformed container.
 */
fl_kq_cp_result_t fl_kq_cp_head_author(const char *path, char *out, size_t cap);

/**
 * Sets *sec_out to the newest successful signature or countersignature event (history types 3, 4)
 * in epoch seconds, 0 when there is none. Returns IO when the container cannot be read.
 */
fl_kq_cp_result_t fl_kq_cp_last_signature(const char *path, int64_t *sec_out);

/**
 * Builds a history bundle's letters, signed by host_key and sealed to requester_key: l15 the head
 * revision (kind 15), l17 the history snapshot (kind 17). Who may ask is the caller's gate; caller
 * frees both. Returns NO_KEY (unknown host or requester), FORKED, IO (no container).
 */
fl_kq_cp_result_t fl_kq_cp_history_letters_with(const fl_kq_keydir_t *keys, const char *host_key,
                                                const uint8_t host_signing_secret[32], const char *requester_key,
                                                const char *path, fl_kq_buf_t *l15, fl_kq_buf_t *l17);

/**
 * Checks the file's current bytes in as a new revision by author_key on the container's head and
 * writes the container back. Caller wipes signing_secret. Returns FORKED with two heads, IO when the
 * file or container cannot be read, INVALID for a damaged container or a refused check-in.
 */
fl_kq_cp_result_t fl_kq_cp_sign_head_with(const char *path, const char *author_key,
                                          const uint8_t signing_secret[32], const char *created_at_utc);

/** Display names of the history's type and outcome codes; "?" past the table. Never parsed. */
const char *fl_kq_cp_event_name(uint8_t code);
const char *fl_kq_cp_outcome_name(uint8_t code);

/**
 * One history line from fl_kq_cp_history as display text (names for the codes, "UNKNOWN" for
 * an empty actor). Returns the length written to out, which never exceeds cap - 1.
 */
size_t fl_kq_cp_format_event(const char *line, size_t n, char *out, size_t cap);

/** Called once per history line (no trailing newline) from fl_kq_cp_for_each_event. A nonzero
 *  return stops the walk. line points into the walked buffer and is NOT NUL-terminated: use n.
 *  It is valid only during the call; copy what must outlive it. */
typedef int (*fl_kq_cp_event_fn)(const char *line, size_t n, void *ctx);

/**
 * Walks the newline-terminated lines of a buffer filled by fl_kq_cp_history, in order, calling
 * fn for each (pair it with fl_kq_cp_format_event for display). A final fragment with no
 * terminating newline is not a line and is skipped. Returns the number of lines passed to fn,
 * the one that stopped the walk included; 0 means there was no history. A NULL buffer or fn
 * visits nothing. The buffer must not be freed or modified while the walk runs, callback included.
 */
size_t fl_kq_cp_for_each_event(const fl_kq_buf_t *lines, fl_kq_cp_event_fn fn, void *ctx);

/**
 * Permit (supervisor): countersigns revision rev in the container at container_path as
 * supervisor_key, after the author's signature verifies under keys, and writes the container
 * back in place. The recipient accepts a newer copy only with this countersignature in it.
 */
fl_kq_cp_result_t fl_kq_cp_permit_with(const fl_kq_keydir_t *keys, const char *supervisor_key,
                                       const uint8_t supervisor_secret[32], const char *path,
                                       const uint8_t rev[32]);

/**
 * Decode checkpoint (recipient). Opens letter with box_secret (signed by sender_key under
 * sender_signing_public, addressed to me_key), attaches my_sig, and writes dest_path only
 * when KeyQuorum reports the delivery ready (author signature, trust, co-signature, single
 * head) and the copy is FIRST or NEWER than what dest_path's sidecars recorded. A file
 * already at dest_path with no sidecar is never overwritten (EXISTS).
 */
fl_kq_cp_result_t fl_kq_cp_decode_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_public[32],
                                       const char *me_key, const uint8_t box_secret[32],
                                       const fl_kq_buf_t *letter, const kq_file_offer_t *offer,
                                       const uint8_t my_sig[64], const char *dest_path);

/**
 * fl_kq_cp_decode_with for a landing directory the caller already holds (dir, from the
 * storage table's dir_open or from fl_server_shared_resolve_local_path's fd) and the plain name
 * inside it. The checkpoint uses that handle for every read and write and never opens the
 * directory again by path, so a directory replaced after the caller checked it is not followed;
 * the caller keeps ownership of the handle. Same results as fl_kq_cp_decode_with.
 */
fl_kq_cp_result_t fl_kq_cp_decode_in_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                          const uint8_t sender_signing_public[32],
                                          const char *me_key, const uint8_t box_secret[32],
                                          const fl_kq_buf_t *letter, const kq_file_offer_t *offer,
                                          const uint8_t my_sig[64], const fl_kq_dir_t *dir,
                                          const char *leaf);

/* ---- refusal reports (KQER, keyquorum_error.h) ------------------------------------- */

/** Sidecar suffix of a KQER refusal report, stored as <dir>/<64 lower-case hex of the ref>.kqer. */
#define FL_KQ_REPORT_SUFFIX ".kqer"

/** Encodes the offer's wire bytes, exactly as net_kq_delivery.c sends them (u64 big-endian). */
void fl_kq_cp_offer_encode(const kq_file_offer_t *o, uint8_t out[FL_KQ_OFFER_WIRE_LEN]);
/** Decodes an offer from its wire bytes. */
void fl_kq_cp_offer_decode(const uint8_t in[FL_KQ_OFFER_WIRE_LEN], kq_file_offer_t *o);

/** Largest KQER KeyQuorum encodes (the read bound). */
#define FL_KQ_REPORT_MAX 1024u

/** Most reports one landing directory keeps, and the ledger that lists their refs. */
#define FL_KQ_REPORT_CAP 64u
#define FL_KQ_REPORT_LEDGER "reports" FL_KQ_REPORT_SUFFIX

/** Writes dir/<hex ref>.kqer to out. Returns 0, or -1 when it does not fit or an argument is missing. */
int fl_kq_cp_report_path(const char *dir, const uint8_t ref[32], char *out, size_t cap);

/**
 * Signs a KQER about ref as actor and writes it, with the ledger, inside the held directory dir.
 * Caller wipes secret. Returns OK, EXISTS (nothing rewritten), TOO_LARGE (FL_KQ_REPORT_CAP reached,
 * nothing written), INVALID (fields refused), IO (directory unreadable or unwritable).
 */
fl_kq_cp_result_t fl_kq_cp_report_write_in(uint16_t code, uint8_t class_code, uint8_t step,
                                           const uint8_t ref[32], const char *actor,
                                           const char *occurred_at, const uint8_t secret[32],
                                           const fl_kq_dir_t *dir);

/**
 * Reads the report for ref through the held directory and returns it only when it verifies as a
 * report about ref signed by reporter_public. Caller frees out (fl_kq_buf_free).
 * Returns OK, IO (no such report), BAD_SIGNATURE (not that report; out is empty).
 */
fl_kq_cp_result_t fl_kq_cp_report_read_in(const fl_kq_dir_t *dir, const uint8_t ref[32],
                                          const uint8_t reporter_public[32], fl_kq_buf_t *out);

/**
 * Verifies kqer under reporter_public and that it names ref, then appends one ErrorReported event
 * (history type 48) to the container beside path. A report decides no trust.
 * Returns BAD_SIGNATURE for a refused report (nothing written), IO without a container.
 */
fl_kq_cp_result_t fl_kq_cp_inject_report_bytes(const uint8_t reporter_public[32], const uint8_t *kqer,
                                               size_t kqer_len, const uint8_t ref[32], const char *path);

/**
 * As fl_kq_cp_inject_report_bytes, for the KQER file at report_path under reporter_key's key in keys.
 * Returns NO_KEY when keys has no signing key for reporter_key, IO when a file cannot be read or written.
 */
fl_kq_cp_result_t fl_kq_cp_inject_report_with(const fl_kq_keydir_t *keys, const char *reporter_key,
                                              const char *report_path, const uint8_t ref[32],
                                              const char *path);

/* ---- the native shell's account store (kq_checkpoint_users.c) --------------------- */

/** Computes the SHA-256 of the offer's wire bytes, the artifact a refusal report names. OK or ERR. */
fl_result_t fl_kq_cp_offer_ref(const kq_file_offer_t *o, uint8_t ref[32]);

/* user_db.h is native-only; its type is named here so the lab can use this header. */
typedef struct fl_user_db fl_user_db_t;

/** The native user store as a key directory: base first, so &dir.base is what checkpoints read. */
typedef struct {
    fl_kq_keydir_t      base;
    const fl_user_db_t *db;
} fl_user_db_keydir_t;

/** Points dir at db's signing and box public keys. */
void fl_user_db_keydir_init(fl_user_db_keydir_t *dir, const fl_user_db_t *db);

/* The checkpoints above over the native user store (NULL users is INVALID, as before). */
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
/** fl_kq_cp_decode for a landing directory the caller holds (fl_kq_cp_decode_in_with). */
fl_kq_cp_result_t fl_kq_cp_decode_in(const fl_user_db_t *users, const char *sender_key,
                                     const uint8_t sender_signing_public[32], const char *me_key,
                                     const uint8_t box_secret[32], const fl_kq_buf_t *letter,
                                     const kq_file_offer_t *offer, const uint8_t my_sig[64],
                                     const fl_kq_dir_t *dir, const char *leaf);

#endif /* FL_KQ_CHECKPOINT_H */
