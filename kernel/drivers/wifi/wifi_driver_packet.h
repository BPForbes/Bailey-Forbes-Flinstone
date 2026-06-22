/*
 * WiFi driver ↔ P3 packet module bridge.
 * Validates and parses frames through contract_p3_packet / net_wire vocabulary.
 */

#ifndef KERNEL_DRIVERS_WIFI_DRIVER_PACKET_H
#define KERNEL_DRIVERS_WIFI_DRIVER_PACKET_H

#include "contract_result.h"
#include "net_packet.h"

#include <stddef.h>
#include <stdint.h>

/** Validate an outbound L2 frame before coprocessor / FullMAC TX. */
fl_result_t wifi_driver_packet_validate_tx(const uint8_t *frame, size_t len);

/**
 * Parse a received frame into the P3 RX pipeline (L2 → L3 → L4 when applicable).
 * Returns FL_RESULT_OK when at least L2 is valid; IPv4 parse failure still OK for ARP.
 */
fl_result_t wifi_driver_packet_ingest_rx(const uint8_t *frame, size_t len,
					 fl_net_pipeline_rx_t *pipe_out);

#endif /* KERNEL_DRIVERS_WIFI_DRIVER_PACKET_H */
