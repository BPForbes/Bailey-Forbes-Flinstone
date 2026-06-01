#include "contract_p5_file_delivery.h"
#include "server_shared_fs.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_landed_basename_prefers_file_name(void)
{
    fl_server_file_offer_t a;
    fl_server_file_offer_t b;
    fl_server_file_offer_t spaced;
    fl_server_file_offer_t bad;
    char name_a[256];
    char name_b[256];
    char name_spaced[256];

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&spaced, 0, sizeof(spaced));
    memset(&bad, 0, sizeof(bad));
    strncpy(a.share_id, "share-100-1", sizeof(a.share_id) - 1u);
    strncpy(a.file_name, "joke.txt", sizeof(a.file_name) - 1u);
    strncpy(b.share_id, "share-200-2", sizeof(b.share_id) - 1u);
    strncpy(b.file_name, "joke.txt", sizeof(b.file_name) - 1u);
    strncpy(spaced.share_id, "share-100-1", sizeof(spaced.share_id) - 1u);
    strncpy(spaced.file_name, "quarterly report.txt", sizeof(spaced.file_name) - 1u);
    strncpy(bad.share_id, "share-100-1", sizeof(bad.share_id) - 1u);
    strncpy(bad.file_name, "bad/name.txt", sizeof(bad.file_name) - 1u);

    ASSERT(fl_server_shared_landed_basename(&a, name_a, sizeof(name_a)) == FL_RESULT_OK);
    ASSERT(fl_server_shared_landed_basename(&b, name_b, sizeof(name_b)) == FL_RESULT_OK);
    ASSERT(strcmp(name_a, "joke.txt") == 0);
    ASSERT(strcmp(name_b, "joke.txt") == 0);
    ASSERT(fl_server_shared_landed_basename(&spaced, name_spaced, sizeof(name_spaced)) ==
           FL_RESULT_OK);
    ASSERT(strcmp(name_spaced, "quarterly report.txt") == 0);
    ASSERT(fl_server_shared_landed_basename(&bad, name_spaced, sizeof(name_spaced)) ==
           FL_RESULT_INVAL);
    return 0;
}

static int test_save_disambiguates_same_file_name(void)
{
    fl_server_file_offer_t first;
    fl_server_file_offer_t second;
    const uint8_t payload[] = "x";
    char path[512];

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    strncpy(first.share_id, "share-a", sizeof(first.share_id) - 1u);
    strncpy(first.file_name, "joke.txt", sizeof(first.file_name) - 1u);
    strncpy(second.share_id, "share-b", sizeof(second.share_id) - 1u);
    strncpy(second.file_name, "joke.txt", sizeof(second.file_name) - 1u);

    ASSERT(fl_server_shared_save_offer(&first, payload, 1u, path, sizeof(path)) ==
           FL_RESULT_OK);
    ASSERT(access("server_shared/joke.txt", F_OK) == 0);
    ASSERT(fl_server_shared_save_offer(&second, payload, 1u, path, sizeof(path)) ==
           FL_RESULT_OK);
    ASSERT(access("server_shared/joke (2).txt", F_OK) == 0);
    (void)remove("server_shared/joke.txt");
    (void)remove("server_shared/joke.txt.meta");
    (void)remove("server_shared/joke (2).txt");
    (void)remove("server_shared/joke (2).txt.meta");
    return 0;
}

int main(void)
{
    if (test_landed_basename_prefers_file_name() != 0)
        return 1;
    if (test_save_disambiguates_same_file_name() != 0)
        return 1;
    printf("All server_shared landed name tests passed.\n");
    return 0;
}
