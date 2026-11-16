/* A fake session server: three members, no sockets. It defines the symbols net_kq_delivery.c links
 * against, so one translation unit per test binary includes it. */
#ifndef KQ_NET_FAKE_H
#define KQ_NET_FAKE_H

#include "net_kq_delivery.h"
#include "user_db.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static fl_net_server_member_t s_members[3];
static fl_net_server_t *s_srv = (fl_net_server_t *)&s_members;

size_t fl_net_server_member_count(const fl_net_server_t *srv) {
    (void)srv;
    return 3;
}

const fl_net_server_member_t *fl_net_server_member_at(const fl_net_server_t *srv, size_t i) {
    (void)srv;
    return i < 3 ? &s_members[i] : NULL;
}

void fl_net_server_set_kq_hook(fl_net_server_kq_hook_fn hook) { (void)hook; }
void fl_net_client_set_kq_hook(fl_net_client_kq_hook_fn hook) { (void)hook; }

fl_result_t fl_net_session_send_frame(fl_net_sock_handle_t h, uint8_t op, const uint8_t *p, uint16_t n) {
    (void)h; (void)op; (void)p; (void)n;
    return FL_RESULT_OK;
}

static const fl_net_server_member_t *s_actor;
static char s_last_err[512];

static inline void kq_net_fake_note_error(uint8_t opcode, const uint8_t *payload, uint16_t plen) {
    if (opcode == 0x7Fu) {
        size_t n = plen < sizeof s_last_err - 1 ? plen : sizeof s_last_err - 1;

        memcpy(s_last_err, payload, n);
        s_last_err[n] = '\0';
    }
}

static int64_t s_clock = 1000000;
static int64_t fake_now(void) { return s_clock; }

static fl_user_db_t s_db;
static const fl_user_db_t *the_users(void) { return &s_db; }

static inline void member(int i, const char *principal, int is_host) {
    memset(&s_members[i], 0, sizeof s_members[i]);
    s_members[i].in_use = 1;
    s_members[i].is_host = (uint8_t)is_host;
    s_members[i].member_id = (fl_net_server_member_id_t)(i + 1);
    s_members[i].peer_handle = FL_NET_SOCK_INVALID;
    snprintf(s_members[i].principal, sizeof s_members[i].principal, "%.63s", principal);
}

#endif
