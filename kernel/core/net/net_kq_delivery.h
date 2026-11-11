#ifndef FL_NET_KQ_DELIVERY_H
#define FL_NET_KQ_DELIVERY_H

/*
 * KeyQuorum co-signed delivery over the session wire (FL_NET_SESSION_OP_KQ_*,
 * contracts/drivers/contract_p4_keyquorum.h). This module only carries what the
 * checkpoints (kq_checkpoint.c) produce and decide:
 *
 *   sender     OFFER + PART...            -> host
 *   host       verifies the author, holds the letter, ANNOUNCE -> recipient
 *   recipient  COSIGN                     -> host
 *   host       verifies the co-signer; at threshold RELEASE + PART... -> recipient
 *   recipient  decodes; ACK               -> host (letter deleted)
 *   anyone     KEYS: public keys from the host's account registry
 *
 * The host is the single key authority: it answers KEYS from its account store and checks
 * every signature against it. It never holds a decryption key.
 */

#include "contract_result.h"
#include "fl/driver/keyquorum.h"
#include "kq_checkpoint.h"
#include "kq_delivery.h"
#include "net_client.h"
#include "net_server.h"

#include <stdint.h>

/**
 * The host's key authority (kq_keydir.h): every signature is checked against it and KEYS is
 * answered from it. The native shell's is its account store; the browser lab's is its
 * account table and the keys members publish.
 */
typedef const fl_kq_keydir_t *(*fl_net_kq_keys_fn)(void);

/**
 * Sets the key authority and where the host keeps deliveries and held letters (opened when
 * the host first needs it). Attaches no transport: the lab delivers frames to
 * fl_net_kq_host_handle / fl_net_kq_client_handle itself.
 */
fl_result_t fl_net_kq_configure(fl_net_kq_keys_fn keys, const char *store_dir);

/** The account store the host checks signatures against (the shell's session store). */
typedef const fl_user_db_t *(*fl_net_kq_users_fn)(void);

/**
 * Native shell (net_kq_delivery_users.c): configures over the account store and installs
 * the server and client hooks, so session frames reach the handlers below.
 */
fl_result_t fl_net_kq_install(fl_net_kq_users_fn users, const char *store_dir);

/* Host side (also reachable directly when the host's own user acts). */
void fl_net_kq_host_handle(fl_net_server_t *srv, const fl_net_server_member_t *from,
                           uint8_t opcode, const uint8_t *payload, uint16_t plen);
/* Client side. */
void fl_net_kq_client_handle(fl_net_client_t *client, uint8_t opcode, const uint8_t *payload,
                             uint16_t plen);

/** Send one KQ frame from this shell: straight to the host handler when hosting. */
fl_result_t fl_net_kq_send(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                           uint8_t opcode, const uint8_t *payload, uint16_t plen);

/* ---- what the shell's verbs need ------------------------------------------ */

typedef struct {
    char    key_id[FL_HERITAGE_KEY_ID_MAX];
    int     found;
    uint8_t signing_public[32];
    uint8_t box_public[32];
} fl_net_kq_keys_t;

/** Ask the host for `who`'s public keys (an account name or key id); waits up to wait_ms. */
fl_result_t fl_net_kq_lookup(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                             const char *who, unsigned wait_ms, fl_net_kq_keys_t *out);

/**
 * The keys a KEYS reply already brought for `who`, without asking again: OK, NOENT (the
 * host holds none) or TIMEDOUT (no reply yet). For a caller that cannot block, such as the
 * browser lab's single thread: fl_net_kq_lookup with wait_ms 0 asks, and this reads the
 * answer once it has arrived.
 */
fl_result_t fl_net_kq_keys_cached(const char *who, fl_net_kq_keys_t *out);

/** Send an encoded delivery: OFFER then the letter in parts. */
fl_result_t fl_net_kq_send_delivery(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                    const char *sender_key, const char *recipient_key,
                                    const fl_kq_encoded_t *enc);

/** Ask the host to announce (and re-release) every delivery waiting for this account. */
fl_result_t fl_net_kq_pull(fl_net_server_t *srv, fl_net_client_t *client, int hosting);
/** Send this account's co-signature on a delivery. */
fl_result_t fl_net_kq_send_cosign(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                  const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const char *coauthor,
                                  const uint8_t sig[64]);
/** Tell the host the letter was decoded (accepted 1) or refused (0). */
fl_result_t fl_net_kq_send_ack(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                               const uint8_t id[FL_KQ_DELIVERY_ID_LEN], int accepted);

typedef struct {
    uint8_t         id[FL_KQ_DELIVERY_ID_LEN];
    char            sender[FL_HERITAGE_KEY_ID_MAX];
    char            file_name[128];
    kq_file_offer_t offer;
    int64_t         expires_at;
    int             cosigned;
    uint8_t         sig[64];
    int             letter_ready;
} fl_net_kq_inbox_t;

/** On the host: every delivery the account sender offered here, newest first (any state,
 *  from the host catalog); at most cap. 0 on a shell that holds no deliveries. */
size_t fl_net_kq_host_sent(const char *sender, fl_kq_delivery_t *out, size_t cap);
/** Co-signature requests this shell has received; at most cap. */
size_t fl_net_kq_inbox(fl_net_kq_inbox_t *out, size_t cap);
/** Remembers this shell's co-signature for a request, so a later open can attach it. */
fl_result_t fl_net_kq_inbox_set_cosig(const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                                      const uint8_t sig[64]);
/** The released letter for a request, once complete; waits up to wait_ms. Caller frees. */
fl_result_t fl_net_kq_take_letter(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], unsigned wait_ms,
                                  fl_kq_buf_t *letter);
/** Forget a request after it was decoded or refused. */
void fl_net_kq_inbox_drop(const uint8_t id[FL_KQ_DELIVERY_ID_LEN]);


/**
 * A member asks the host for a delivery's history: REQ [request id 16][delivery id 16], answered
 * with DATA chunks of the history bundle (kq_history_bundle.h: the kind-15 letter then the
 * kind-17 letter, both sealed to the requester). Two things are the host's to provide, and with
 * either missing the host refuses (it never answers silently and never guesses):
 *  - the gate: 1 when this requester may have this delivery's history (the native shell asks
 *    fl_authz_subsystem_check(FL_AUTHZ_OP_FILE_HISTORY_EXPORT, ...));
 *  - the source: fills out[cap] with the bundle, signed by the host's own account (so that
 *    account must be signed in) and sealed to the requester's box key.
 */
typedef int (*fl_net_kq_history_gate_fn)(const char *requester_key, const uint8_t delivery_id[FL_KQ_DELIVERY_ID_LEN]);
typedef fl_result_t (*fl_net_kq_history_source_fn)(const char *requester_key,
                                                   const uint8_t delivery_id[FL_KQ_DELIVERY_ID_LEN],
                                                   uint8_t *out, size_t cap, size_t *out_len);
void fl_net_kq_set_history(fl_net_kq_history_gate_fn gate, fl_net_kq_history_source_fn source);

fl_result_t fl_net_kq_request_history(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                      const uint8_t delivery_id[FL_KQ_DELIVERY_ID_LEN],
                                      uint8_t request_id_out[16]);
/** The history for a request once complete (waits up to wait_ms): both letters, copied; the caller
 *  frees them with fl_kq_buf_free. TIMEDOUT while incomplete, NOENT for an unknown request. */
fl_result_t fl_net_kq_history_take(const uint8_t request_id[16], unsigned wait_ms,
                                   fl_kq_buf_t *letter_file, fl_kq_buf_t *letter_snapshot);
void fl_net_kq_history_drop(const uint8_t request_id[16]);

/* ---- refusal report (KQ-12) ---------------------------------------------------------------- */

/**
 * The recipient of a refused delivery sends the signed KQER it wrote beside the refused file (the bytes of
 * <hex ref>.kqer, at most FL_KQ_REPORT_FRAME_MAX) to the host, after the refusing fl_net_kq_send_ack. The
 * host forwards it to the delivery's sender only when the sender of this frame is that delivery's
 * recipient, the delivery was refused, and the host has not forwarded a report for it already.
 * INVAL for an empty or oversized report.
 */
fl_result_t fl_net_kq_send_report(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                  const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const uint8_t *kqer, size_t len);

/** A report that reached this shell as the delivery's sender: copied out and removed. NOENT when none is
 *  held for id, NOMEM (nothing removed) when cap is too small. The report is a claim by its signer: apply it
 *  with fl_kq_cp_inject_report_with, which checks the signature and the artifact. */
fl_result_t fl_net_kq_report_take(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], uint8_t *out, size_t cap, size_t *len);
/** How many reports are held (at most four; a fifth replaces the oldest). */
size_t fl_net_kq_reports_pending(void);

/* ---- tests ------------------------------------------------------------------ */

/** Replace how the host reaches members and how a client reaches the host (tests). */
typedef void (*fl_net_kq_host_send_fn)(fl_net_server_t *srv, const fl_net_server_member_t *to,
                                       uint8_t opcode, const uint8_t *payload, uint16_t plen);
typedef void (*fl_net_kq_client_send_fn)(fl_net_client_t *client, uint8_t opcode,
                                         const uint8_t *payload, uint16_t plen);
void fl_net_kq_set_transport(fl_net_kq_host_send_fn host_send,
                             fl_net_kq_client_send_fn client_send);
/** Replace the host clock (tests). NULL restores fl_time_wall_sec. */
void fl_net_kq_set_clock(int64_t (*now)(void));
/** Drop all in-memory state and close the host store (tests). */
void fl_net_kq_reset(void);

#endif /* FL_NET_KQ_DELIVERY_H */
