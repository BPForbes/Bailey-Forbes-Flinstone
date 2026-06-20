/*
 * WiFi Coprocessor UART Transport Layer
 * Handles communication with ESP32/ESP8266 WiFi modules over UART
 * Implements AT command protocol and serial framing
 */

#ifndef KERNEL_DRIVERS_WIFI_UART_TRANSPORT_H
#define KERNEL_DRIVERS_WIFI_UART_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "kernel/drivers/wifi_coprocessor.h"

#define WIFI_UART_BUFFER_SIZE     4096
#define WIFI_UART_RX_TIMEOUT_MS   5000
#define WIFI_UART_TX_TIMEOUT_MS   1000
#define WIFI_UART_CMD_TIMEOUT_MS  10000

typedef enum {
	WIFI_UART_BAUD_115200 = 115200,
	WIFI_UART_BAUD_230400 = 230400,
	WIFI_UART_BAUD_460800 = 460800,
	WIFI_UART_BAUD_921600 = 921600,
} wifi_uart_baud_t;

typedef enum {
	WIFI_UART_PARITY_NONE,
	WIFI_UART_PARITY_EVEN,
	WIFI_UART_PARITY_ODD,
} wifi_uart_parity_t;

typedef struct {
	int uart_fd;                    /* UART file descriptor or handle */
	wifi_uart_baud_t baud;
	wifi_uart_parity_t parity;

	/* Buffers for RX/TX */
	uint8_t rx_buffer[WIFI_UART_BUFFER_SIZE];
	size_t rx_head;
	size_t rx_tail;

	uint8_t tx_buffer[WIFI_UART_BUFFER_SIZE];
	size_t tx_head;
	size_t tx_tail;

	/* State */
	bool connected;
	uint32_t last_command_ms;
	uint32_t last_response_ms;
	uint32_t error_count;

	/* Cached AT+CWLAP scan results */
	wifi_network_t scan_results[WIFI_MAX_NETWORKS];
	uint16_t scan_count;

	/* Staged raw RX frame from UART poll (+IPD / transparent mode) */
	uint8_t rx_staging[1518];
	size_t rx_staging_len;

	/* Parsed Ethernet payload extracted from +IPD framing */
	uint8_t rx_payload[1518];
	size_t rx_payload_len;
} wifi_uart_context_t;

/* Initialization */
int wifi_uart_init(wifi_uart_context_t *ctx, int uart_fd, wifi_uart_baud_t baud);
int wifi_uart_deinit(wifi_uart_context_t *ctx);

/* Low-level I/O */
int wifi_uart_send_raw(wifi_uart_context_t *ctx, const uint8_t *data, size_t len);
int wifi_uart_receive_raw(wifi_uart_context_t *ctx, uint8_t *buffer, size_t buf_len,
			  size_t *out_len, uint32_t timeout_ms);

/* AT Command interface */
int wifi_uart_send_command(wifi_uart_context_t *ctx, const char *cmd);
int wifi_uart_read_response(wifi_uart_context_t *ctx, char *buffer, size_t buf_len,
			    uint32_t timeout_ms);
int wifi_uart_expect_response(wifi_uart_context_t *ctx, const char *expected,
			      uint32_t timeout_ms);

/* Coprocessor initialization over UART */
int wifi_uart_coproc_create(const char *name, int uart_fd, wifi_uart_baud_t baud,
			    wifi_coproc_t **out_coproc);

/* Polling/async */
int wifi_uart_poll(wifi_uart_context_t *ctx);

/* Status */
bool wifi_uart_is_connected(wifi_uart_context_t *ctx);
int wifi_uart_get_stats(wifi_uart_context_t *ctx, uint32_t *rx_count,
		       uint32_t *tx_count, uint32_t *errors);

#endif /* KERNEL_DRIVERS_WIFI_UART_TRANSPORT_H */
