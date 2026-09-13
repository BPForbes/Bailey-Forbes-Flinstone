#ifndef KERNEL_DRIVERS_WIFI_LAB_ROUTER_H
#define KERNEL_DRIVERS_WIFI_LAB_ROUTER_H

#include "contract_p3_dhcp.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Simulated lab AP DHCP server (execution in driver layer).
 * Core net orchestrates DISCOVER/REQUEST via fl_net_dhcp_acquire; this module
 * builds OFFER/ACK with per-station leases on the lab LAN.
 */

void wifi_lab_router_reset(void);

void wifi_lab_sta_mac(uint8_t mac[6]);

fl_result_t wifi_lab_router_dhcp_exchange(const uint8_t cli_mac[6], const uint8_t *req,
                                          size_t req_len, uint8_t *reply, size_t reply_cap,
                                          size_t *reply_len);

#endif
