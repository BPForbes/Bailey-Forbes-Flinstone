#include "net_wifi_mgmt.h"

#define FL_WIFI_MGMT_HDR_MIN 24u

int fl_net_wifi_mgmt_hdr_valid(const uint8_t *frame, size_t len) {
    if (!frame || len < FL_WIFI_MGMT_HDR_MIN)
        return 0;
    /* FC type = management (00), subtype in upper nibble of byte 0. */
    if ((frame[0] & 0x0cu) != 0u)
        return 0;
    return 1;
}
