#include "net_server.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t fl_net_server_broadcast_except(fl_net_server_t *srv,
                                           fl_net_server_member_id_t except_id,
                                           uint8_t opcode,
                                           const uint8_t *payload,
                                           uint16_t plen)
{
    (void)srv;
    (void)except_id;
    (void)opcode;
    (void)payload;
    (void)plen;
    return FL_RESULT_OK;
}

fl_result_t fl_net_server_send_to_member(fl_net_server_t *srv,
                                         fl_net_server_member_id_t member_id,
                                         uint8_t opcode,
                                         const uint8_t *payload,
                                         uint16_t plen)
{
    (void)srv;
    (void)member_id;
    (void)opcode;
    (void)payload;
    (void)plen;
    return FL_RESULT_OK;
}

fl_result_t fl_net_session_send_frame(int peer_handle,
                                      uint8_t opcode,
                                      const uint8_t *payload,
                                      uint16_t plen)
{
    (void)peer_handle;
    (void)opcode;
    (void)payload;
    (void)plen;
    return FL_RESULT_OK;
}
