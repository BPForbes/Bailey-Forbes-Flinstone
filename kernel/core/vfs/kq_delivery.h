#ifndef FL_KQ_DELIVERY_H
#define FL_KQ_DELIVERY_H

/*
 * Host side of co-signed delivery: the "latent space" (contract_p4_keyquorum.h).
 *
 * Each delivery is a small integrate-and-fire unit. A verified signature is a spike that
 * raises its potential; at FL_KQ_HOST_THRESHOLD it fires (the sealed letter is released)
 * exactly once and is then refractory; an unanswered delivery leaks away when its
 * server-clock expiry passes, and its letter is deleted. The host stores letters it
 * cannot open and never holds a decryption key. Signatures are checked by the caller
 * (kq_checkpoint.c) before a spike is integrated.
 *
 * The store is the host catalog: one FL_CHANNEL_PAYLOAD_KQ row per delivery in
 * <dir>/host_catalog.db (status HELD, RELEASED, DELIVERED or EXPIRED, its key ids and offer in
 * catalog_kq, its history in catalog_event) plus one file per held letter in <dir>/kq_letters,
 * where <dir> is the server share root the caller names. The share path never reads these
 * rows. The store keeps its own connection to the file and its own lock: the server's network
 * thread and shell commands both reach it. A row is never deleted, so an id is held once.
 */

#include "contract_p4_keyquorum.h"
#include "contract_p5_server_catalog.h"
#include "contract_result.h"
#include "heritage.h"
#include "keyquorum_files.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FL_KQ_SPIKE_REQUEST = 1,
    FL_KQ_SPIKE_OFFER = 2,
    FL_KQ_SPIKE_ANNOUNCE = 3,
    FL_KQ_SPIKE_COSIGN = 4,
    FL_KQ_SPIKE_RELEASE = 5,
    FL_KQ_SPIKE_ACK = 6,
} fl_kq_spike_t;

typedef enum {
    /** Below threshold: wait for more signatures. */
    FL_KQ_NEURON_REST = 0,
    /** Threshold reached now: release the letter. */
    FL_KQ_NEURON_FIRE = 1,
    /** Already fired: never release twice. */
    FL_KQ_NEURON_REFRACTORY = 2,
    /** Expired before reaching threshold. */
    FL_KQ_NEURON_LEAKED = 3,
} fl_kq_neuron_t;

typedef struct fl_kq_delivery {
    uint8_t         id[FL_KQ_DELIVERY_ID_LEN];
    char            sender[FL_HERITAGE_KEY_ID_MAX];
    char            recipient[FL_HERITAGE_KEY_ID_MAX];
    char            file_name[128];
    kq_file_offer_t offer;
    uint32_t        potential;
    uint32_t        threshold;
    int64_t         offered_at;
    int64_t         expires_at;
    int             fired;
    int             acknowledged;
    int             expired; /* the store ended it (letter deleted); set only by the store */
    int             has_cosig;
    uint8_t         cosig[64];
} fl_kq_delivery_t;

/** A new delivery for an offer whose author signature the host verified (potential 1). */
void fl_kq_delivery_init(fl_kq_delivery_t *d, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                         const char *sender, const char *recipient, const char *file_name,
                         const kq_file_offer_t *offer, int64_t now, int64_t ttl_sec);

/**
 * Integrate one verified spike at server time `now`. Only COSIGN raises the potential
 * (the author's spike is counted by init); a second co-signature counts once.
 */
fl_kq_neuron_t fl_kq_delivery_integrate(fl_kq_delivery_t *d, fl_kq_spike_t spike,
                                        const uint8_t *cosig, int64_t now);

typedef struct fl_kq_store fl_kq_store_t;

fl_result_t fl_kq_store_open(const char *dir, fl_kq_store_t **out);
void        fl_kq_store_close(fl_kq_store_t *s);

/** Records a new delivery and its sealed letter. BUSY when the id is already held. */
fl_result_t fl_kq_store_put(fl_kq_store_t *s, const fl_kq_delivery_t *d, const uint8_t *letter,
                            size_t letter_len);
fl_result_t fl_kq_store_get(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                            fl_kq_delivery_t *out);
/**
 * Saves a spike the caller integrated. COSIGN on a HELD delivery stores the co-signature and,
 * when it fired, releases it (status, release time, "cosigned" and "released" events in one
 * transaction). ACK with acknowledged set on a RELEASED delivery marks it DELIVERED. NOENT when
 * the delivery is not in that state any more (released, delivered or expired meanwhile).
 */
fl_result_t fl_kq_store_update(fl_kq_store_t *s, const fl_kq_delivery_t *d, fl_kq_spike_t spike,
                               int64_t now);
/** The held letter; the caller frees *letter. NOENT once delivered or leaked. */
fl_result_t fl_kq_store_letter(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                               uint8_t **letter, size_t *len);
/** Deletes the held letter after the recipient acknowledged it; the log stays. */
fl_result_t fl_kq_store_forget_letter(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN]);
/** Live deliveries for recipient (held before expiry, or released and awaiting the receipt),
 *  oldest first; at most cap. */
size_t      fl_kq_store_pending_for(fl_kq_store_t *s, const char *recipient, int64_t now,
                                    fl_kq_delivery_t *out, size_t cap);
/** Every delivery sender offered, whatever its state, newest first; at most cap. */
size_t      fl_kq_store_sent_by(fl_kq_store_t *s, const char *sender, fl_kq_delivery_t *out,
                                size_t cap);
/** One word for a delivery's state as the host sees it (waiting for co-signature, released,
 *  delivered, expired). */
const char *fl_kq_delivery_state(const fl_kq_delivery_t *d);
/**
 * Leak: ends deliveries held past their expiry or released FL_KQ_RELEASE_TTL_SEC ago without a
 * receipt. Their rows become EXPIRED with an "expired" event, in one transaction; their letters
 * are deleted once that has committed. Returns how many (0 when nothing changed).
 */
size_t      fl_kq_store_leak(fl_kq_store_t *s, int64_t now);
/** A delivery's history (offered, cosigned, released, acknowledged, expired), oldest first. */
fl_result_t fl_kq_store_events(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                               fl_server_catalog_event_t *out, size_t cap, size_t *count);

void fl_kq_delivery_id_hex(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], char out[2 * FL_KQ_DELIVERY_ID_LEN + 1]);
int  fl_kq_delivery_id_parse(const char *hex, uint8_t id[FL_KQ_DELIVERY_ID_LEN]);

#endif /* FL_KQ_DELIVERY_H */
