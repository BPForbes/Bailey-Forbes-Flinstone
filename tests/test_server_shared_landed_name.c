#include "contract_p5_file_delivery.h"
#include "server_shared_fs.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_landed_basename(void)
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
    ASSERT(strcmp(name_a, "share-100-1_joke.txt") == 0);
    ASSERT(strcmp(name_b, "share-200-2_joke.txt") == 0);
    ASSERT(strcmp(name_a, name_b) != 0);
    ASSERT(fl_server_shared_landed_basename(&spaced, name_spaced, sizeof(name_spaced)) ==
           FL_RESULT_OK);
    ASSERT(strcmp(name_spaced, "share-100-1_quarterly report.txt") == 0);
    ASSERT(fl_server_shared_landed_basename(&bad, name_spaced, sizeof(name_spaced)) ==
           FL_RESULT_INVAL);
    return 0;
}

int main(void)
{
    if (test_landed_basename() != 0)
        return 1;
    printf("All server_shared landed name tests passed.\n");
    return 0;
}
