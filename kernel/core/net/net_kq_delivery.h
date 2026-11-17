#ifndef FL_NET_KQ_DELIVERY_H
#define FL_NET_KQ_DELIVERY_H

/* Session-wire transport for KeyQuorum deliveries. The host is the single key authority and never holds a decryption key. */

#include "contract_result.h"
#include "fl/driver/keyquorum.h"
#include "kq_checkpoint.h"
#include "kq_delivery.h"
#include "net_client.h"
#include "net_server.h"

#include <stdint.h>

typedef const fl_kq_keydir_t *(*fl_net_kq_keys_fn)(void);

fl_result_t fl_net_kq_configure(fl_net_kq_keys_fn keys, const char *store_dir);

typedef const fl_user_db_t *(*fl_net_kq_users_fn)(void);

fl_result_t fl_net_kq_install(fl_net_kq_users_fn users, const char *store_dir);

void fl_net_kq_host_handle(fl_net_server_t *srv, const fl_net_server_member_t *from,
                           uint8_t opcode, const uint8_t *payload, uint16_t plen);
void fl_net_kq_client_handle(fl_net_client_t *client, uint8_t opcode, const uint8_t *payload,
                             uint16_t plen);

fl_result_t fl_net_kq_send(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                           uint8_t opcode, const uint8_t *payload, uint16_t plen);

typedef struct {
    char    key_id[FL_HERITAGE_KEY_ID_MAX];
    int     found;
    uint8_t signing_public[32];
    uint8_t box_public[32];
} fl_net_kq_keys_t;

fl_result_t fl_net_kq_lookup(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                             const char *who, unsigned wait_ms, fl_net_kq_keys_t *out);

/* With wait_ms 0, fl_net_kq_lookup asks; this reads the answer once it has arrived. */
fl_result_t fl_net_kq_keys_cached(const char *who, fl_net_kq_keys_t *out);

fl_result_t fl_net_kq_send_delivery(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                    const char *sender_key, const char *recipient_key,
                                    const fl_kq_encoded_t *enc);

fl_result_t fl_net_kq_pull(fl_net_server_t *srv, fl_net_client_t *client, int hosting);
fl_result_t fl_net_kq_send_cosign(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                  const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const char *coauthor,
                                  const uint8_t sig[64]);
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

size_t fl_net_kq_host_sent(const char *sender, fl_kq_delivery_t *out, size_t cap);
size_t fl_net_kq_inbox(fl_net_kq_inbox_t *out, size_t cap);
fl_result_t fl_net_kq_inbox_set_cosig(const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                                      const uint8_t sig[64]);
fl_result_t fl_net_kq_take_letter(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], unsigned wait_ms,
                                  fl_kq_buf_t *letter);
void fl_net_kq_inbox_drop(const uint8_t id[FL_KQ_DELIVERY_ID_LEN]);

/* Gate and source are the host's: with either missing it refuses (never silently). */
typedef int (*fl_net_kq_history_gate_fn)(const char *requester_key, const uint8_t delivery_id[FL_KQ_DELIVERY_ID_LEN]);
typedef fl_result_t (*fl_net_kq_history_source_fn)(const char *requester_key,
                                                   const uint8_t delivery_id[FL_KQ_DELIVERY_ID_LEN],
                                                   uint8_t *out, size_t cap, size_t *out_len);
void fl_net_kq_set_history(fl_net_kq_history_gate_fn gate, fl_net_kq_history_source_fn source);

fl_result_t fl_net_kq_request_history(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                      const uint8_t delivery_id[FL_KQ_DELIVERY_ID_LEN],
                                      uint8_t request_id_out[16]);
fl_result_t fl_net_kq_history_take(const uint8_t request_id[16], unsigned wait_ms,
                                   fl_kq_buf_t *letter_file, fl_kq_buf_t *letter_snapshot);
void fl_net_kq_history_drop(const uint8_t request_id[16]);
#define FL_NET_KQ_HISTORY_OTHER_FILE ((fl_result_t)-74)
/* Caller frees lines and wipes box_secret; TIMEDOUT leaves the request pending. */
fl_result_t fl_net_kq_history_open(const uint8_t request_id[16], unsigned wait_ms, const uint8_t box_secret[32],
                                   const char *sender, const char *me, const uint8_t sender_public[32],
                                   const uint8_t expected_file_id[16], fl_kq_buf_t *lines,
                                   uint32_t *snapshot_events);

fl_result_t fl_net_kq_send_report(fl_net_server_t *srv, fl_net_client_t *client, int hosting,
                                  const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const uint8_t *kqer, size_t len);

fl_result_t fl_net_kq_report_take(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], uint8_t *out, size_t cap, size_t *len);
size_t fl_net_kq_reports_pending(void);

typedef void (*fl_net_kq_host_send_fn)(fl_net_server_t *srv, const fl_net_server_member_t *to,
                                       uint8_t opcode, const uint8_t *payload, uint16_t plen);
typedef void (*fl_net_kq_client_send_fn)(fl_net_client_t *client, uint8_t opcode,
                                         const uint8_t *payload, uint16_t plen);
void fl_net_kq_set_transport(fl_net_kq_host_send_fn host_send,
                             fl_net_kq_client_send_fn client_send);
void fl_net_kq_set_clock(int64_t (*now)(void));
void fl_net_kq_reset(void);

#endif /* FL_NET_KQ_DELIVERY_H */
