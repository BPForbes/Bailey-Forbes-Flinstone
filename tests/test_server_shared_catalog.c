#include "contract_p5_server_catalog.h"
#include "server_shared_db.h"

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while (0)

static int test_catalog_file_roundtrip(void)
{
    char tmp[] = "/tmp/fl_catalog_test_XXXXXX";
    fl_server_file_offer_t offer;
    const uint8_t payload[] = "catalog blob payload";
    fl_server_catalog_entry_t entry;
    uint8_t out[64];
    size_t out_len = 0;
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];

    ASSERT(mkdtemp(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-catalog-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "note.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;

    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("share-catalog-1", &entry) == FL_RESULT_OK);
    ASSERT(entry.status == FL_SERVER_CATALOG_COMPLETE);
    ASSERT(entry.payload_kind == FL_CHANNEL_PAYLOAD_FILE);
    strncpy(hash, entry.content_hash, sizeof(hash) - 1u);

    ASSERT(fl_server_catalog_member_can_fetch(&entry, 3u, 0u) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_member_can_fetch(&entry, 4u, 0u) == FL_RESULT_ACCES);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 3u, 0u, &entry, out, sizeof(out),
                                           &out_len) == FL_RESULT_OK);
    ASSERT(out_len == sizeof(payload) - 1u);
    ASSERT(memcmp(out, payload, out_len) == 0);

    (void)fl_server_catalog_close();
    return 0;
}

static int test_catalog_message_public(void)
{
    char tmp[] = "/tmp/fl_catalog_msg_XXXXXX";
    const uint8_t body[] = "hello catalog";
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];
    fl_server_catalog_entry_t entry;

    ASSERT(mkdtemp(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    ASSERT(fl_server_catalog_store_message(2u, 0u, "broadcast", body, sizeof(body) - 1u,
                                         0u, FL_FILE_FLAG_PUBLIC,
                                         NULL, 0, hash, sizeof(hash)) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 5u, 0u, &entry, NULL, 0, NULL) ==
           FL_RESULT_OK);
    ASSERT(entry.is_public == 1u);
    (void)fl_server_catalog_close();
    return 0;
}

int main(void)
{
    if (test_catalog_file_roundtrip() != 0)
        return 1;
    if (test_catalog_message_public() != 0)
        return 1;
    printf("All server_shared catalog tests passed.\n");
    return 0;
}
