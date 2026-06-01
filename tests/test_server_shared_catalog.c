#include "contract_p5_server_catalog.h"
#include "server_shared_db.h"

#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static int test_catalog_large_blob_fetch(void)
{
    char tmp[] = "/tmp/fl_catalog_large_XXXXXX";
    fl_server_file_offer_t offer;
    uint8_t payload[5000];
    fl_server_catalog_entry_t entry;
    uint8_t *out = NULL;
    size_t out_len = 0;
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];
    size_t i;

    for (i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i & 0xffu);

    ASSERT(mkdtemp(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-large-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "large.bin", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;

    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, sizeof(payload)) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("share-large-1", &entry) == FL_RESULT_OK);
    strncpy(hash, entry.content_hash, sizeof(hash) - 1u);

    out = (uint8_t *)malloc(sizeof(payload));
    ASSERT(out != NULL);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 3u, 0u, &entry, NULL, 0, NULL) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_read_blob(&entry, out, sizeof(payload), &out_len) ==
           FL_RESULT_OK);
    ASSERT(out_len == sizeof(payload));
    ASSERT(memcmp(out, payload, out_len) == 0);
    ASSERT(fl_server_catalog_read_blob(&entry, out, sizeof(payload) - 1u, &out_len) ==
           FL_RESULT_NOMEM);

    free(out);
    (void)fl_server_catalog_close();
    return 0;
}

static int test_catalog_duplicate_hash_two_transfers(void)
{
    char tmp[] = "/tmp/fl_catalog_dup_XXXXXX";
    fl_server_file_offer_t offer_a;
    fl_server_file_offer_t offer_b;
    const uint8_t payload[] = "same bytes different transfer";
    fl_server_catalog_entry_t entry_a;
    fl_server_catalog_entry_t entry_b;
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];

    ASSERT(mkdtemp(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    memset(&offer_a, 0, sizeof(offer_a));
    strncpy(offer_a.share_id, "share-dup-a", sizeof(offer_a.share_id) - 1u);
    strncpy(offer_a.file_name, "a.txt", sizeof(offer_a.file_name) - 1u);
    offer_a.sender_member_id = 2u;
    offer_a.receiver_member_id = 3u;
    offer_a.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;

    memset(&offer_b, 0, sizeof(offer_b));
    strncpy(offer_b.share_id, "share-dup-b", sizeof(offer_b.share_id) - 1u);
    strncpy(offer_b.file_name, "b.txt", sizeof(offer_b.file_name) - 1u);
    offer_b.sender_member_id = 4u;
    offer_b.receiver_member_id = 5u;
    offer_b.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;

    ASSERT(fl_server_catalog_register_file_offer(&offer_a) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer_a, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_register_file_offer(&offer_b) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer_b, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);

    ASSERT(fl_server_catalog_lookup_transfer("share-dup-a", &entry_a) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("share-dup-b", &entry_b) == FL_RESULT_OK);
    ASSERT(entry_a.status == FL_SERVER_CATALOG_COMPLETE);
    ASSERT(entry_b.status == FL_SERVER_CATALOG_COMPLETE);
    ASSERT(strcmp(entry_a.content_hash, entry_b.content_hash) == 0);
    strncpy(hash, entry_a.content_hash, sizeof(hash) - 1u);

    ASSERT(fl_server_catalog_member_can_fetch(&entry_a, 3u, 0u) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_member_can_fetch(&entry_b, 5u, 0u) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_member_can_fetch(&entry_a, 5u, 0u) == FL_RESULT_ACCES);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 3u, 0u, &entry_a, NULL, 0, NULL) ==
           FL_RESULT_OK);
    ASSERT(strcmp(entry_a.transfer_id, "share-dup-a") == 0);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 5u, 0u, &entry_b, NULL, 0, NULL) ==
           FL_RESULT_OK);
    ASSERT(strcmp(entry_b.transfer_id, "share-dup-b") == 0);

    (void)fl_server_catalog_close();
    return 0;
}

static int test_catalog_import_handoff(void)
{
    char src[] = "/tmp/fl_catalog_src_XXXXXX";
    char dst[] = "/tmp/fl_catalog_dst_XXXXXX";
    fl_server_file_offer_t offer;
    const uint8_t payload[] = "handoff blob";
    fl_server_catalog_entry_t entry;

    ASSERT(mkdtemp(src) != NULL);
    ASSERT(mkdtemp(dst) != NULL);
    {
        char shared[512];
        snprintf(shared, sizeof(shared), "%s/server_shared", src);
        ASSERT(mkdir(shared, 0700) == 0 || errno == EEXIST);
        if (chdir(src) != 0)
            return 1;
        strncpy(g_cwd, src, sizeof(g_cwd) - 1u);
        g_cwd[sizeof(g_cwd) - 1u] = '\0';
    }
    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-handoff-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "handoff.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;
    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);
    (void)fl_server_catalog_close();

    {
        char shared[512];
        snprintf(shared, sizeof(shared), "%s/server_shared", dst);
        ASSERT(mkdir(shared, 0700) == 0 || errno == EEXIST);
        if (chdir(dst) != 0)
            return 1;
        strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);
        g_cwd[sizeof(g_cwd) - 1u] = '\0';
    }
    {
        char src_shared[512];
        snprintf(src_shared, sizeof(src_shared), "%s/server_shared", src);
        ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_OK);
    }
    ASSERT(fl_server_catalog_lookup_transfer("share-handoff-1", &entry) == FL_RESULT_OK);
    ASSERT(entry.status == FL_SERVER_CATALOG_COMPLETE);
    (void)fl_server_catalog_close();
    return 0;
}

int main(void)
{
    if (test_catalog_file_roundtrip() != 0)
        return 1;
    if (test_catalog_message_public() != 0)
        return 1;
    if (test_catalog_large_blob_fetch() != 0)
        return 1;
    if (test_catalog_duplicate_hash_two_transfers() != 0)
        return 1;
    if (test_catalog_import_handoff() != 0)
        return 1;
    printf("All server_shared catalog tests passed.\n");
    return 0;
}
