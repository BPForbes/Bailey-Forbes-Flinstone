#ifndef NET_FILE_DELIVERY_H
#define NET_FILE_DELIVERY_H

#include "contract_p3_session_wire.h"
#include "contract_p5_file_delivery.h"
#include "net_client.h"
#include "net_server.h"

#include <stdint.h>

typedef struct {
    uint16_t member_id;
    uint8_t disambig_index;
    char principal[FL_SERVER_PRINCIPAL_MAX];
    char nick[FL_SERVER_NICK_MAX];
} fl_server_file_member_ref_t;

void fl_server_file_lookup_begin(void);
fl_result_t fl_server_file_lookup_push(const fl_server_file_member_ref_t *entry);

fl_result_t fl_server_file_store_offer(const fl_server_file_offer_t *offer);

fl_result_t fl_net_file_send_offer(fl_net_server_t *srv,
                                   fl_net_client_t *client,
                                   int hosting,
                                   fl_net_server_member_id_t sender_id,
                                   const fl_server_file_offer_t *offer);

fl_result_t fl_net_file_host_relay(fl_net_server_t *srv,
                                   fl_net_server_member_id_t sender_id,
                                   uint8_t opcode,
                                   const uint8_t *payload,
                                   uint16_t plen);

fl_result_t fl_net_file_send_control(fl_net_server_t *srv,
                                     fl_net_client_t *client,
                                     int hosting,
                                     fl_net_server_member_id_t actor_id,
                                     uint8_t opcode,
                                     const uint8_t *payload,
                                     uint16_t plen);

fl_result_t fl_net_file_send_file_contents(fl_net_server_t *srv,
                                           fl_net_client_t *client,
                                           int hosting,
                                           fl_net_server_member_id_t sender_id,
                                           const fl_server_file_offer_t *offer,
                                           const char *local_path);

fl_result_t fl_net_file_store_chunk(const uint8_t *payload, uint16_t plen);

fl_result_t fl_net_file_store_done(const uint8_t *payload, uint16_t plen);

fl_result_t fl_net_file_send_meta(fl_net_server_t *srv,
                                  fl_net_client_t *client,
                                  int hosting,
                                  fl_net_server_member_id_t sender_id,
                                  const fl_server_file_offer_t *offer);

fl_result_t fl_net_file_store_meta(const uint8_t *payload, uint16_t plen);

#endif /* NET_FILE_DELIVERY_H */
