#include "contract_p5_file_delivery.h"
#include "server_shared_fs.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_landed_basename(void)
{
    fl_server_file_offer_t a;
    fl_server_file_offer_t b;
    char name_a[256];
    char name_b[256];

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    strncpy(a.share_id, "share-100-1", sizeof(a.share_id) - 1u);
    strncpy(a.file_name, "joke.txt", sizeof(a.file_name) - 1u);
    strncpy(b.share_id, "share-200-2", sizeof(b.share_id) - 1u);
    strncpy(b.file_name, "joke.txt", sizeof(b.file_name) - 1u);

    ASSERT(fl_server_shared_landed_basename(&a, name_a, sizeof(name_a)) == FL_RESULT_OK);
    ASSERT(fl_server_shared_landed_basename(&b, name_b, sizeof(name_b)) == FL_RESULT_OK);
    ASSERT(strcmp(name_a, "joke.txt") == 0);
    ASSERT(strcmp(name_b, "joke.txt") == 0);
    return 0;
}

int main(void)
{
    if (test_landed_basename() != 0)
        return 1;
    printf("All server_shared landed name tests passed.\n");
    return 0;
}
