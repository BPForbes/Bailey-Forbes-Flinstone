#ifndef FL_KQ_DELIVERY_H
#define FL_KQ_DELIVERY_H


/* Holds letters it cannot open, never a decryption key. */
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
    FL_KQ_SPIKE_REFUSE = 7,
} fl_kq_spike_t;

typedef enum {
    FL_KQ_NEURON_REST = 0,
    FL_KQ_NEURON_FIRE = 1,
    FL_KQ_NEURON_REFRACTORY = 2,
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
    int             refused; /* the recipient refused it (letter deleted); set only by the store */
    int             has_cosig;
    uint8_t         cosig[64];
} fl_kq_delivery_t;

void fl_kq_delivery_init(fl_kq_delivery_t *d, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                         const char *sender, const char *recipient, const char *file_name,
                         const kq_file_offer_t *offer, int64_t now, int64_t ttl_sec);

fl_kq_neuron_t fl_kq_delivery_integrate(fl_kq_delivery_t *d, fl_kq_spike_t spike,
                                        const uint8_t *cosig, int64_t now);

typedef struct fl_kq_store fl_kq_store_t;

fl_result_t fl_kq_store_open(const char *dir, fl_kq_store_t **out);
void        fl_kq_store_close(fl_kq_store_t *s);

/* BUSY when the id is already held. */
fl_result_t fl_kq_store_put(fl_kq_store_t *s, const fl_kq_delivery_t *d, const uint8_t *letter,
                            size_t letter_len);
fl_result_t fl_kq_store_get(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                            fl_kq_delivery_t *out);
fl_result_t fl_kq_store_update(fl_kq_store_t *s, const fl_kq_delivery_t *d, fl_kq_spike_t spike,
                               int64_t now);
/* The caller frees *letter; NOENT once delivered or leaked. */
fl_result_t fl_kq_store_letter(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                               uint8_t **letter, size_t *len);
fl_result_t fl_kq_store_forget_letter(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN]);
size_t      fl_kq_store_pending_for(fl_kq_store_t *s, const char *recipient, int64_t now,
                                    fl_kq_delivery_t *out, size_t cap);
size_t      fl_kq_store_sent_by(fl_kq_store_t *s, const char *sender, fl_kq_delivery_t *out,
                                size_t cap);
const char *fl_kq_delivery_state(const fl_kq_delivery_t *d);
size_t      fl_kq_store_leak(fl_kq_store_t *s, int64_t now);
fl_result_t fl_kq_store_events(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                               fl_server_catalog_event_t *out, size_t cap, size_t *count);

fl_result_t fl_kq_store_claim_report(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN], int64_t at);

void fl_kq_delivery_id_hex(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], char out[2 * FL_KQ_DELIVERY_ID_LEN + 1]);
int  fl_kq_delivery_id_parse(const char *hex, uint8_t id[FL_KQ_DELIVERY_ID_LEN]);

#endif /* FL_KQ_DELIVERY_H */
