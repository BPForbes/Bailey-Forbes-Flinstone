#include "net_wifi_sae.h"

fl_result_t fl_net_wifi_sae_derive_pmk(const char *ssid, const char *passphrase,
                                       uint8_t *pmk_out, size_t pmk_cap) {
    (void)ssid;
    (void)passphrase;
    (void)pmk_out;
    (void)pmk_cap;
    return FL_RESULT_NOSYS;
}
