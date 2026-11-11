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
 * True when name is a plain file name a delivery may use: fl_fs_name_is_plain, and not ending
 * in one of the suffixes the delivery keeps beside a file (.part, .meta, .kqtf, .kqseen, .kqer,
 * .kqbak; the list is fl_server_shared_name_is_reserved).
 * Every name that arrives over the wire (offer, announcement) is checked before it is stored,
 * printed or joined to a directory.
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
 * The author label of the file's single head revision (its container beside path), the label
 * KeyQuorum names (kq_file_head_author), NUL-terminated into out. FORKED with two heads, which
 * have no author until they are merged; IO when the container cannot be read (none beside path
 * included); INVALID for a container with no revision, a label longer than cap - 1 or a malformed
 * container. out is "" on every result but OK.
 */
fl_kq_cp_result_t fl_kq_cp_head_author(const char *path, char *out, size_t cap);

/**
 * When the file at path was last signed: the newest successful RevisionSigned or CountersignatureAdded
 * event in the history of the container beside it (KeyQuorum event types 3 and 4), as seconds since the
 * epoch (UTC). *sec_out is 0 when the history holds no signature. IO when the container cannot be read.
 */
fl_kq_cp_result_t fl_kq_cp_last_signature(const char *path, int64_t *sec_out);

/**
 * The two letters of a history bundle (FL-04) for the file at path, both signed by host_key and sealed to
 * requester_key: l15 is the head revision resealed to the requester (the file letter, kind 15) and l17 the
 * snapshot of the whole event history (kind 17). Pack them with fl_history_bundle_pack. Nothing is checked in
 * and nothing is written. Any account that holds the container can seal it: KeyQuorum checks the revision's own
 * author signature, not the sealer, so who may ask is the caller's gate. NO_KEY when keys does not know the
 * host or the requester, FORKED with two heads, IO without a container. l15 and l17 are the caller's to free (fl_kq_buf_free).
 */
fl_kq_cp_result_t fl_kq_cp_history_letters_with(const fl_kq_keydir_t *keys, const char *host_key,
                                                const uint8_t host_signing_secret[32], const char *requester_key,
                                                const char *path, fl_kq_buf_t *l15, fl_kq_buf_t *l17);

/**
 * Signs the file as author_key: checks the file's current bytes in on the current head of the container
 * beside path (KeyQuorum kq_file_check_in: a new revision signed by the author) and writes the container
 * back through the storage table. The author's key is the caller's copy to wipe. FORKED with two heads,
 * IO when the file or container cannot be read, INVALID for a damaged container or a refused check-in.
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

/**
 * A KQER is a signed report about one refused artifact, kept as a sidecar named by the artifact's
 * SHA-256: <dir>/<64 lower-case hex>.kqer. The artifact of a refused co-signed delivery is its
 * public offer as it travels (fl_kq_cp_offer_encode), which the sender, the host and the recipient
 * all hold; its SHA-256 is fl_kq_cp_offer_ref (native, kq_checkpoint_users.c).
 */
#define FL_KQ_REPORT_SUFFIX ".kqer"

/** The offer's wire bytes, exactly what net_kq_delivery.c sends (u64 big-endian). */
void fl_kq_cp_offer_encode(const kq_file_offer_t *o, uint8_t out[FL_KQ_OFFER_WIRE_LEN]);

/** Largest KQER KeyQuorum encodes (MAX_ENCODED_LEN, src/error_report.rs): the read bound. */
#define FL_KQ_REPORT_MAX 1024u

/**
 * Most reports one landing directory keeps. A report's name is the SHA-256 of sender-chosen
 * offer bytes, so each distinct refused offer would be a new file; past this many nothing more
 * is written. The ones written are listed by ref in FL_KQ_REPORT_LEDGER inside the directory,
 * replaced together with each report (one commit), so the count is read through the held
 * directory and never by enumerating a path.
 */
#define FL_KQ_REPORT_CAP 64u
#define FL_KQ_REPORT_LEDGER "reports" FL_KQ_REPORT_SUFFIX

/** dir/<hex ref>.kqer into out; 0, or -1 when it does not fit or an argument is missing. */
int fl_kq_cp_report_path(const char *dir, const uint8_t ref[32], char *out, size_t cap);

/**
 * Signs a KQER (code and class from keyquorum_error.h's closed sets, step the caller's flow
 * step) about the artifact whose SHA-256 is ref, as actor with signing secret secret (the
 * caller's to wipe), at occurred_at (UTC text), and writes <hex ref>.kqer inside the held
 * directory dir (the handle the decode used; never reopened by path) together with the ledger.
 * OK written; EXISTS when that report is already there (nothing rewritten, not counted again);
 * TOO_LARGE when FL_KQ_REPORT_CAP reports are already listed (nothing written); INVALID when
 * KeyQuorum refuses the fields; IO when the directory cannot be read or written. No free text
 * is ever written.
 */
fl_kq_cp_result_t fl_kq_cp_report_write_in(uint16_t code, uint8_t class_code, uint8_t step,
                                           const uint8_t ref[32], const char *actor,
                                           const char *occurred_at, const uint8_t secret[32],
                                           const fl_kq_dir_t *dir);

/**
 * The same injection for a report already in memory (one that arrived over the wire, fl_net_kq_report_take):
 * verifies kqer under reporter_public and that it names ref (the SHA-256 of the artifact, fl_kq_cp_offer_ref),
 * then appends one ErrorReported event to the container beside path. A report is a claim by its signer: it
 * decides no trust. BAD_SIGNATURE for any report KeyQuorum refuses (nothing is written), IO without a
 * container.
 */
fl_kq_cp_result_t fl_kq_cp_inject_report_bytes(const uint8_t reporter_public[32], const uint8_t *kqer,
                                               size_t kqer_len, const uint8_t ref[32], const char *path);

/**
 * Consumer: reads the KQER at report_path, and appends one ErrorReported event (history type 48)
 * to the container beside path, written back in place, when KeyQuorum verifies it under
 * reporter_key's signing key in keys and it names the artifact ref the caller holds.
 * BAD_SIGNATURE when it does not verify or names another artifact (nothing written), NO_KEY
 * when keys has no signing key for reporter_key, IO when a file cannot be read or written.
 */
fl_kq_cp_result_t fl_kq_cp_inject_report_with(const fl_kq_keydir_t *keys, const char *reporter_key,
                                              const char *report_path, const uint8_t ref[32],
                                              const char *path);

/* ---- the native shell's account store (kq_checkpoint_users.c) --------------------- */

/** The SHA-256 of the offer's wire bytes: the artifact a refusal report names. OK or ERR. */
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
