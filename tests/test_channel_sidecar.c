#include "contract_p3_channel_sidecar.h"
#include "contract_p5_file_delivery.h"
#include "contract_pkt_channel_meta.h"
#include "net_channel_sidecar.h"
#include "net_file_delivery.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_sidecar_roundtrip(void)
{
    fl_server_file_offer_t offer;
    uint8_t wire[256];
    uint16_t wire_len = 0;
    fl_channel_sidecar_t sidecar;
    fl_channel_sidecar_t decoded;

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-100-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "joke.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 5u;
    offer.expires_at = 1906558245u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_FLAG_EXPIRES;

    ASSERT(fl_server_file_meta_from_offer(&offer, &sidecar) == FL_RESULT_OK);
    sidecar.center_freq_hz = 5180000000ull;
    ASSERT(fl_channel_sidecar_encode(&sidecar, wire, sizeof(wire), &wire_len) == FL_RESULT_OK);
    ASSERT(wire_len > FL_PKT_CHANNEL_META_LEN);
    ASSERT(fl_pkt_wire_valid(wire, FL_PKT_CHANNEL_META_LEN));
    ASSERT(fl_channel_sidecar_decode(wire, wire_len, &decoded) == FL_RESULT_OK);
    ASSERT(strcmp(decoded.transfer_id, offer.share_id) == 0);
    ASSERT(strcmp(decoded.payload_name, offer.file_name) == 0);
    ASSERT(decoded.expires_at == offer.expires_at);
    ASSERT(decoded.sender_member_id == offer.sender_member_id);
    ASSERT(decoded.receiver_member_id == offer.receiver_member_id);
    ASSERT(decoded.center_freq_hz == 5180000000ull);
    ASSERT(decoded.payload_kind == FL_CHANNEL_PAYLOAD_FILE);
    return 0;
}

static int test_sidecar_forward_acl(void)
{
    ASSERT(fl_channel_sidecar_may_forward_to(0u, 3u) == 1);
    ASSERT(fl_channel_sidecar_may_forward_to(3u, 3u) == 1);
    ASSERT(fl_channel_sidecar_may_forward_to(2u, 3u) == 0);
    return 0;
}

static int test_legacy_meta_decode(void)
{
    fl_channel_sidecar_t decoded;
    static const uint8_t wire[] = {
        0x00, 0x0c, 0x73, 0x68, 0x61, 0x72, 0x65, 0x2d, 0x6c, 0x65, 0x67, 0x61, 0x63, 0x79,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xe8,
        0x00, 0x07, 0x6f, 0x6c, 0x64, 0x2e, 0x74, 0x78, 0x74
    };
    const uint16_t wire_len = (uint16_t)sizeof(wire);

    ASSERT(fl_channel_sidecar_decode(wire, wire_len, &decoded) == FL_RESULT_OK);
    ASSERT(strcmp(decoded.transfer_id, "share-legacy") == 0);
    ASSERT(decoded.expires_at == 1000u);
    ASSERT(strcmp(decoded.payload_name, "old.txt") == 0);
    ASSERT(decoded.payload_kind == FL_CHANNEL_PAYLOAD_FILE);
    return 0;
}

int main(void)
{
    if (test_sidecar_roundtrip() != 0)
        return 1;
    if (test_sidecar_forward_acl() != 0)
        return 1;
    if (test_legacy_meta_decode() != 0)
        return 1;
    printf("All channel sidecar tests passed.\n");
    return 0;
}
