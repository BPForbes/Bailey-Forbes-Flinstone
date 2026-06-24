#ifndef WIFI_FULLMAC_AFPACKET_H
#define WIFI_FULLMAC_AFPACKET_H

/**
 * Physical FullMAC data plane: AF_PACKET raw L2 TX/RX (#328).
 * Driver-layer NIC execution; core net_wifi_fullmac orchestrates only.
 */

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t wifi_fullmac_afpacket_open(unsigned int ifindex, int *fd_out);
void wifi_fullmac_afpacket_close(int *fd);

fl_result_t wifi_fullmac_afpacket_send(int fd, uint32_t ifindex, const uint8_t *frame, size_t len);

fl_result_t wifi_fullmac_afpacket_recv(int fd, uint8_t *buf, size_t cap, size_t *len_out);

#endif /* WIFI_FULLMAC_AFPACKET_H */
