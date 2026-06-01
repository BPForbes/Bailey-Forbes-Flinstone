#include "contract_p3_packet.h"
#include "contract_p3_session_wire.h"
#include "contract_p5_file_delivery.h"

#include <stdio.h>
#include <string.h>

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

    ASSERT(fl_server_file_meta_from_offer(&offer, &meta) == FL_RESULT_OK);
    ASSERT(strcmp(meta.share_id, offer.share_id) == 0);
    ASSERT(strcmp(meta.file_name, "joke.txt") == 0);
    ASSERT(meta.expires_at == offer.expires_at);

    ASSERT(fl_file_packet_encode_meta(&meta, wire, sizeof(wire), &wire_len) == FL_RESULT_OK);
    ASSERT(wire_len > 0u);
    ASSERT(fl_file_packet_decode_meta(wire, wire_len, &decoded) == FL_RESULT_OK);
    ASSERT(strcmp(decoded.share_id, meta.share_id) == 0);
    ASSERT(strcmp(decoded.file_name, meta.file_name) == 0);
    ASSERT(decoded.expires_at == meta.expires_at);
    return 0;
}

int main(void)
{
    if (test_meta_roundtrip() != 0)
        return 1;
    printf("All server file meta tests passed.\n");
    return 0;
}
