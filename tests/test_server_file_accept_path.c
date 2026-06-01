#include "contract_p3_packet.h"
#include "net_file_delivery.h"
#include "server_shared_fs.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_accept_after_session_meta(void)
{
    fl_server_file_offer_t offer;
    fl_server_file_meta_t meta;
    uint8_t wire[FL_NET_SESSION_MAX_MSG];
    uint16_t wire_len = 0;
    const char payload[] = "joke punchline\n";
    uint8_t chunk_wire[FL_NET_SESSION_MAX_MSG];
    uint16_t chunk_len = 0;
    uint8_t done_wire[FL_NET_SESSION_MAX_MSG];
    uint16_t done_len = 0;

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-test-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "joke.txt", sizeof(offer.file_name) - 1u);
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;
    offer.file_size = sizeof(payload) - 1u;
    offer.total_chunks = 1u;

    ASSERT(fl_server_file_store_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_file_meta_from_offer(&offer, &meta) == FL_RESULT_OK);
    ASSERT(fl_file_packet_encode_meta(&meta, wire, sizeof(wire), &wire_len) == FL_RESULT_OK);
    ASSERT(fl_net_file_store_meta(wire, wire_len) == FL_RESULT_OK);

    {
        fl_server_file_chunk_header_t chunk;
        memset(&chunk, 0, sizeof(chunk));
        strncpy(chunk.share_id, offer.share_id, sizeof(chunk.share_id) - 1u);
        chunk.chunk_index = 0u;
        chunk.chunk_len = (uint32_t)(sizeof(payload) - 1u);
        chunk.file_offset = 0u;
        ASSERT(fl_file_packet_encode_chunk(&chunk, (const uint8_t *)payload,
                                           chunk.chunk_len, chunk_wire,
                                           sizeof(chunk_wire), &chunk_len) == FL_RESULT_OK);
        ASSERT(fl_net_file_store_chunk(chunk_wire, chunk_len) == FL_RESULT_OK);
    }
    {
        fl_server_file_done_t done;
        memset(&done, 0, sizeof(done));
        strncpy(done.share_id, offer.share_id, sizeof(done.share_id) - 1u);
        done.total_bytes = sizeof(payload) - 1u;
        done.total_chunks = 1u;
        done.status = 1u;
        ASSERT(fl_file_packet_encode_done(&done, done_wire, sizeof(done_wire),
                                          &done_len) == FL_RESULT_OK);
        ASSERT(fl_net_file_store_done(done_wire, done_len) == FL_RESULT_OK);
    }

    ASSERT(fl_server_share_accept(offer.share_id, 3u,
                                  FL_SERVER_FILE_SAVE_TO_SERVER_SHARE) == FL_RESULT_OK);
    {
        char landed[256];
        char path[512];
        ASSERT(fl_server_shared_landed_basename(&offer, landed, sizeof(landed)) ==
               FL_RESULT_OK);
        ASSERT(strcmp(landed, "joke.txt") == 0);
        snprintf(path, sizeof(path), "%s/%s", FL_SERVER_SHARED_DIR_NAME, landed);
        ASSERT(access(path, R_OK) == 0);
    }
    return 0;
}

int main(void)
{
    if (test_accept_after_session_meta() != 0)
        return 1;
    printf("server file accept path test passed.\n");
    return 0;
}
