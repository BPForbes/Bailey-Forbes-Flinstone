#include "common.h"
#include "contract_p3_packet.h"
#include "contract_p3_session_wire.h"
#include "contract_p5_file_delivery.h"
#include "net_file_delivery.h"
#include "server_shared_db.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_meta_roundtrip(void)
{
    fl_server_file_offer_t offer;
    fl_server_file_meta_t meta;
    fl_server_file_meta_t decoded;
    uint8_t wire[FL_NET_SESSION_MAX_MSG];
    uint16_t wire_len = 0;

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-1717198800-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "joke.txt", sizeof(offer.file_name) - 1u);
    offer.expires_at = 1906558245u;
    offer.sender_member_id = 1u;
    offer.receiver_member_id = 2u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_FLAG_EXPIRES;

    ASSERT(fl_server_file_meta_from_offer(&offer, &meta) == FL_RESULT_OK);
    ASSERT(strcmp(meta.transfer_id, offer.share_id) == 0);
    ASSERT(strcmp(meta.payload_name, "joke.txt") == 0);
    ASSERT(meta.expires_at == offer.expires_at);

    ASSERT(fl_file_packet_encode_meta(&meta, wire, sizeof(wire), &wire_len) == FL_RESULT_OK);
    ASSERT(wire_len > 0u);
    ASSERT(fl_file_packet_decode_meta(wire, wire_len, &decoded) == FL_RESULT_OK);
    ASSERT(strcmp(decoded.transfer_id, meta.transfer_id) == 0);
    ASSERT(strcmp(decoded.payload_name, meta.payload_name) == 0);
    ASSERT(decoded.expires_at == meta.expires_at);
    return 0;
}

static fl_net_server_t s_srv;

static void enter_tmp(char *tmp)
{
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';
}

static int count_named(const char *share_id, const char *event, size_t *out)
{
    fl_server_catalog_event_t events[8];
    size_t n = 0;
    size_t i;
    size_t found = 0;

    if (fl_server_catalog_list_events(share_id, events, 8u, &n) != FL_RESULT_OK)
        return -1;
    for (i = 0; i < n; i++) {
        if (strcmp(events[i].event, event) == 0)
            found++;
    }
    *out = found;
    return 0;
}

/* The host command relays with record_member_action 0 and then runs the local
 * share call. That sequence must write one event, and a refused local call
 * must write none. A joined member's frame (record_member_action 1) is still
 * recorded by the relay. */
static int test_host_action_recorded_once(void)
{
    char tmp[] = "/tmp/fl_host_action_once_XXXXXX";
    fl_server_file_offer_t offer;
    uint8_t payload[FL_NET_SESSION_MAX_MSG];
    uint16_t plen = 0;
    size_t found = 0;

    ASSERT(mkdtemp(tmp) != NULL);
    ASSERT(chdir(tmp) == 0);
    enter_tmp(tmp);
    memset(&s_srv, 0, sizeof(s_srv));

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-host-once", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "once.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW;
    ASSERT(fl_server_file_store_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_file_packet_encode_accept(offer.share_id, 3u, FL_SERVER_FILE_DECLINE,
                                        payload, sizeof(payload), &plen) == FL_RESULT_OK);
    ASSERT(fl_net_file_host_relay(&s_srv, 3u, FL_NET_SESSION_OP_FILE_ACCEPT,
                                  payload, plen, 0) == FL_RESULT_OK);
    ASSERT(count_named(offer.share_id, "accepted", &found) == 0);
    ASSERT(found == 0u);
    ASSERT(fl_server_share_accept(offer.share_id, 3u, FL_SERVER_FILE_DECLINE) == FL_RESULT_OK);
    ASSERT(count_named(offer.share_id, "accepted", &found) == 0);
    ASSERT(found == 1u);

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-host-busy", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "busy.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW;
    offer.file_size = 4u;
    ASSERT(fl_server_file_store_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_file_packet_encode_accept(offer.share_id, 3u, FL_SERVER_FILE_DECLINE,
                                        payload, sizeof(payload), &plen) == FL_RESULT_OK);
    ASSERT(fl_net_file_host_relay(&s_srv, 3u, FL_NET_SESSION_OP_FILE_ACCEPT,
                                  payload, plen, 0) == FL_RESULT_OK);
    ASSERT(fl_server_share_accept(offer.share_id, 3u, FL_SERVER_FILE_DECLINE) == FL_RESULT_BUSY);
    ASSERT(count_named(offer.share_id, "accepted", &found) == 0);
    ASSERT(found == 0u);

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-member", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "member.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW;
    ASSERT(fl_server_file_store_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_file_packet_encode_accept(offer.share_id, 3u, FL_SERVER_FILE_SAVE_TO_SERVER_SHARE,
                                        payload, sizeof(payload), &plen) == FL_RESULT_OK);
    ASSERT(fl_net_file_host_relay(&s_srv, 3u, FL_NET_SESSION_OP_FILE_ACCEPT,
                                  payload, plen, 1) == FL_RESULT_OK);
    ASSERT(count_named(offer.share_id, "accepted", &found) == 0);
    ASSERT(found == 1u);
    {
        fl_server_catalog_event_t events[4];
        size_t n = 0;
        ASSERT(fl_server_catalog_list_events(offer.share_id, events, 4u, &n) == FL_RESULT_OK);
        ASSERT(n == 1u);
        ASSERT(strcmp(events[0].detail, "server_shared") == 0);
        ASSERT(events[0].actor_member_id == 3u);
    }

    (void)fl_server_catalog_close();
    return 0;
}

/* Offer decode treats a file name that is not plain as a bad frame (needs net_file_delivery.h). */
static int test_offer_decode_refuses_unsafe_names(void) {
    static const char *const bad[] = {"../x.txt", "sub/y.txt", "..", ".", "a\\b", "ctl\x01n"};
    uint8_t buf[2048];
    uint16_t len = 0;
    fl_server_file_offer_t o;
    fl_server_file_offer_t back;

    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memset(&o, 0, sizeof o);
        strncpy(o.share_id, "share-d", sizeof o.share_id - 1u);
        strncpy(o.file_name, bad[i], sizeof o.file_name - 1u);
        ASSERT(fl_server_file_offer_encode(&o, buf, sizeof buf, &len) == FL_RESULT_OK);
        ASSERT(fl_server_file_offer_decode(buf, len, &back) == FL_RESULT_INVAL);
    }
    memset(&o, 0, sizeof o);
    strncpy(o.share_id, "share-d", sizeof o.share_id - 1u);
    strncpy(o.file_name, "ok name.txt", sizeof o.file_name - 1u);
    ASSERT(fl_server_file_offer_encode(&o, buf, sizeof buf, &len) == FL_RESULT_OK);
    ASSERT(fl_server_file_offer_decode(buf, len, &back) == FL_RESULT_OK);
    ASSERT(strcmp(back.file_name, "ok name.txt") == 0);
    return 0;
}

/* First occurrence of pat (n bytes) inside buf (len bytes), or NULL. */
static uint8_t *find_bytes(uint8_t *buf, size_t len, const char *pat, size_t n) {
    size_t i;

    if (n == 0u || len < n)
        return NULL;
    for (i = 0; i + n <= len; i++)
        if (memcmp(buf + i, pat, n) == 0)
            return buf + i;
    return NULL;
}

/* A text field with an embedded NUL is a bad frame, not a shorter string. */
static int test_offer_decode_refuses_embedded_nul(void) {
    uint8_t buf[2048];
    uint16_t len = 0;
    fl_server_file_offer_t o;
    fl_server_file_offer_t back;
    uint8_t *at;

    memset(&o, 0, sizeof o);
    strncpy(o.share_id, "shXre-n", sizeof o.share_id - 1u);
    strncpy(o.file_name, "abXcd.txt", sizeof o.file_name - 1u);
    ASSERT(fl_server_file_offer_encode(&o, buf, sizeof buf, &len) == FL_RESULT_OK);
    ASSERT(fl_server_file_offer_decode(buf, len, &back) == FL_RESULT_OK);
    at = find_bytes(buf, len, "abXcd.txt", 9u);
    ASSERT(at != NULL);
    at[2] = 0;
    ASSERT(fl_server_file_offer_decode(buf, len, &back) == FL_RESULT_INVAL);
    at[2] = 'X';
    at = find_bytes(buf, len, "shXre-n", 7u);
    ASSERT(at != NULL);
    at[2] = 0;
    ASSERT(fl_server_file_offer_decode(buf, len, &back) == FL_RESULT_INVAL);
    return 0;
}

int main(void)
{
    if (test_meta_roundtrip() != 0)
        return 1;
    if (test_host_action_recorded_once() != 0)
        return 1;
    if (test_offer_decode_refuses_embedded_nul() != 0)
        return 1;
    if (test_offer_decode_refuses_unsafe_names() != 0)
        return 1;
    printf("All server file meta tests passed.\n");
    return 0;
}
