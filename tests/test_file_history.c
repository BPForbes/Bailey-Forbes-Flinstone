#include "contract_p5_server_catalog.h"
#include "server_shared_db.h"

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while (0)

static void enter_tmp(char *tmp)
{
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';
}

static int test_offer_complete_expire_history(void)
{
    char tmp[] = "/tmp/fl_file_history_XXXXXX";
    fl_server_file_offer_t offer;
    const uint8_t payload[] = "history payload";
    fl_server_catalog_event_t events[8];
    size_t count = 0;

    ASSERT(mkdtemp(tmp) != NULL);
    ASSERT(chdir(tmp) == 0);
    enter_tmp(tmp);

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-hist-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "plan.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;
    offer.expires_at = 100u;

    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);

    ASSERT(fl_server_catalog_list_events("share-hist-1", events, 8u, &count) == FL_RESULT_OK);
    ASSERT(count == 2u);
    ASSERT(events[0].seq == 1u);
    ASSERT(strcmp(events[0].event, "offered") == 0);
    ASSERT(events[0].actor_member_id == 2u);
    ASSERT(strcmp(events[0].detail, "plan.txt") == 0);
    ASSERT(events[1].seq == 2u);
    ASSERT(strcmp(events[1].event, "completed") == 0);
    ASSERT(events[1].actor_member_id == 2u);
    ASSERT(strlen(events[1].detail) == 64u);

    ASSERT(fl_server_catalog_purge_expired(200u) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_list_events("share-hist-1", events, 8u, &count) == FL_RESULT_OK);
    ASSERT(count == 3u);
    ASSERT(events[2].seq == 3u);
    ASSERT(strcmp(events[2].event, "expired") == 0);
    ASSERT(events[2].actor_member_id == 0u);
    ASSERT(events[2].at == 200u);

    (void)fl_server_catalog_close();
    return 0;
}

static int test_append_is_ordered_per_share(void)
{
    char tmp[] = "/tmp/fl_file_history_seq_XXXXXX";
    fl_server_catalog_event_t events[8];
    size_t count = 0;

    ASSERT(mkdtemp(tmp) != NULL);
    ASSERT(chdir(tmp) == 0);
    enter_tmp(tmp);

    ASSERT(fl_server_catalog_append_event("share-seq-a", "offered", 2u, 10u, "a.txt") ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_append_event("share-seq-b", "offered", 4u, 11u, "b.txt") ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_append_event("share-seq-a", "accepted", 3u, 12u, "overwrite") ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_append_event("share-seq-a", "revoked", 2u, 13u, "") ==
           FL_RESULT_OK);

    ASSERT(fl_server_catalog_list_events("share-seq-a", events, 8u, &count) == FL_RESULT_OK);
    ASSERT(count == 3u);
    ASSERT(events[0].seq == 1u && strcmp(events[0].event, "offered") == 0);
    ASSERT(events[1].seq == 2u && strcmp(events[1].event, "accepted") == 0);
    ASSERT(events[1].actor_member_id == 3u);
    ASSERT(strcmp(events[1].detail, "overwrite") == 0);
    ASSERT(events[2].seq == 3u && strcmp(events[2].event, "revoked") == 0);

    ASSERT(fl_server_catalog_list_events("share-seq-b", events, 8u, &count) == FL_RESULT_OK);
    ASSERT(count == 1u);
    ASSERT(events[0].seq == 1u);

    ASSERT(fl_server_catalog_list_events("share-seq-a", events, 2u, &count) == FL_RESULT_OK);
    ASSERT(count == 2u);
    ASSERT(strcmp(events[1].event, "accepted") == 0);

    ASSERT(fl_server_catalog_list_events("share-none", events, 8u, &count) == FL_RESULT_OK);
    ASSERT(count == 0u);

    (void)fl_server_catalog_close();
    return 0;
}

int main(void)
{
    if (test_offer_complete_expire_history() != 0)
        return 1;
    if (test_append_is_ordered_per_share() != 0)
        return 1;
    printf("All file history tests passed.\n");
    return 0;
}
