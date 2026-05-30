#ifndef SERVER_BG_H
#define SERVER_BG_H

#include "contract_result.h"
#include "net_client.h"
#include "net_server.h"

/*
 * P3-13 background task wrappers.
 *
 * `fl_server_bg_start_server` spawns one pthread that loops:
 *   - fl_net_server_accept_pending (non-blocking; handles HELLO/announce)
 *   - fl_net_server_poll_members   (non-blocking; routes MSG, NICK, LEAVE)
 *   - sleep ~10ms
 * until `fl_server_bg_stop_server` is called.
 *
 * `fl_server_bg_start_client` spawns one pthread that loops
 * `fl_net_client_poll` and dispatches each frame to the same colour-printing
 * callback that `cmd_server.c` registers on the foreground side.
 *
 * Both APIs are hosted-only (compile on Linux/macOS/BSD). Off-host they
 * return FL_RESULT_NOSYS so a baremetal build still links.
 */

typedef struct fl_server_bg_s fl_server_bg_t;

fl_result_t fl_server_bg_start_server(fl_net_server_t *srv, fl_server_bg_t **handle_out);
fl_result_t fl_server_bg_stop_server(fl_server_bg_t *handle);

typedef void (*fl_server_bg_client_cb)(fl_net_server_event_kind_t kind,
                                       const char *text,
                                       fl_net_server_member_id_t mid,
                                       void *data);

fl_result_t fl_server_bg_start_client(fl_net_client_t *client,
                                      fl_server_bg_client_cb cb, void *data,
                                      fl_server_bg_t **handle_out);
fl_result_t fl_server_bg_stop_client(fl_server_bg_t *handle);

#endif /* SERVER_BG_H */
