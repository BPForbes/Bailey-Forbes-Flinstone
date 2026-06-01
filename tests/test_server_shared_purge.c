#include "contract_p5_file_delivery.h"
#include "server_shared_fs.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_purge_moves_expired_landing(void)
{
    fl_server_file_offer_t offer;
    char landed[256];
    char expired_path[512];
    const uint8_t payload[] = "expired-payload";
    uint64_t now = 2000000000u;

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-expire-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "note.txt", sizeof(offer.file_name) - 1u);
    offer.expires_at = 1000u;

    ASSERT(fl_server_shared_landed_basename(&offer, landed, sizeof(landed)) ==
           FL_RESULT_OK);
    ASSERT(fl_server_shared_save_offer(&offer, payload, sizeof(payload) - 1u,
                                       NULL, 0u) == FL_RESULT_OK);
    ASSERT(fl_server_shared_purge_expired(now) == FL_RESULT_OK);
    snprintf(expired_path, sizeof(expired_path),
             "server_shared/expired/%s", landed);
    ASSERT(access(expired_path, F_OK) == 0);
    ASSERT(fl_server_shared_path_is_expired_quarantine(expired_path) == 1);
    return 0;
}

int main(void)
{
    if (test_purge_moves_expired_landing() != 0)
        return 1;
    printf("All server_shared purge tests passed.\n");
    return 0;
}
