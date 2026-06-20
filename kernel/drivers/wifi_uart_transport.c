/*
 * WiFi Coprocessor UART Transport Implementation
 * Handles serial communication with ESP32/ESP8266 WiFi modules
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "kernel/drivers/wifi_uart_transport.h"

/* AT Command strings for ESP32/ESP8266 */
#define AT_CMD_TEST          "AT\r\n"
#define AT_CMD_SET_MODE_STA  "AT+CWMODE=1\r\n"
#define AT_CMD_SCAN_START    "AT+CWLAP\r\n"
#define AT_CMD_CONNECT       "AT+CWJAP=\"%s\",\"%s\"\r\n"
#define AT_CMD_DISCONNECT    "AT+CWQAP\r\n"
#define AT_CMD_GET_STATUS    "AT+CIPSTATUS\r\n"

/* Response markers */
#define AT_RESP_OK      "OK"
#define AT_RESP_ERROR   "ERROR"
#define AT_RESP_FAIL    "FAIL"

/* Helper: wait for a string in response buffer */
static bool uart_buffer_contains(const uint8_t *buffer, size_t len, const char *str)
{
	if (!buffer || !str || len == 0) {
		return false;
	}

	size_t str_len = strlen(str);
	if (str_len > len) {
		return false;
	}

	for (size_t i = 0; i <= len - str_len; i++) {
		if (memcmp(&buffer[i], str, str_len) == 0) {
			return true;
		}
	}
	return false;
}

int wifi_uart_init(wifi_uart_context_t *ctx, int uart_fd, wifi_uart_baud_t baud)
{
	if (!ctx || uart_fd < 0) {
		return -1;
	}

	memset(ctx, 0, sizeof(wifi_uart_context_t));
	ctx->uart_fd = uart_fd;
	ctx->baud = baud;
	ctx->parity = WIFI_UART_PARITY_NONE;
	ctx->connected = false;

	/* TODO: Configure UART hardware (baud rate, parity, stop bits)
	 * This will depend on platform-specific UART driver
	 */

	return 0;
}

int wifi_uart_deinit(wifi_uart_context_t *ctx)
{
	if (!ctx) {
		return -1;
	}

	memset(ctx, 0, sizeof(wifi_uart_context_t));
	return 0;
}

int wifi_uart_send_raw(wifi_uart_context_t *ctx, const uint8_t *data, size_t len)
{
	if (!ctx || !data || len == 0) {
		return -1;
	}

	/* TODO: Platform-specific write to UART
	 * Should copy data into TX buffer and initiate transmission
	 * For now, stub implementation
	 */

	/* If TX buffer has space, queue the data */
	size_t space = WIFI_UART_BUFFER_SIZE - (ctx->tx_head - ctx->tx_tail);
	if (space < len) {
		return -1; /* Buffer full */
	}

	memcpy(&ctx->tx_buffer[ctx->tx_head % WIFI_UART_BUFFER_SIZE], data, len);
	ctx->tx_head += len;

	return (int)len;
}

int wifi_uart_receive_raw(wifi_uart_context_t *ctx, uint8_t *buffer, size_t buf_len,
			  size_t *out_len, uint32_t timeout_ms)
{
	if (!ctx || !buffer || buf_len == 0 || !out_len) {
		return -1;
	}

	/* TODO: Platform-specific read from UART with timeout
	 * Should read available data from RX buffer
	 * For now, stub implementation
	 */

	size_t available = ctx->rx_head - ctx->rx_tail;
	size_t to_copy = available < buf_len ? available : buf_len;

	if (to_copy > 0) {
		memcpy(buffer, &ctx->rx_buffer[ctx->rx_tail % WIFI_UART_BUFFER_SIZE],
		       to_copy);
		ctx->rx_tail += to_copy;
	}

	*out_len = to_copy;
	return to_copy > 0 ? 0 : -1;
}

int wifi_uart_send_command(wifi_uart_context_t *ctx, const char *cmd)
{
	if (!ctx || !cmd) {
		return -1;
	}

	size_t len = strlen(cmd);
	return wifi_uart_send_raw(ctx, (const uint8_t *)cmd, len);
}

int wifi_uart_read_response(wifi_uart_context_t *ctx, char *buffer, size_t buf_len,
			    uint32_t timeout_ms)
{
	if (!ctx || !buffer || buf_len == 0) {
		return -1;
	}

	/* TODO: Read from UART until OK/ERROR/FAIL with timeout
	 * Stub: just mark as OK for testing
	 */

	size_t out_len = 0;
	int ret = wifi_uart_receive_raw(ctx, (uint8_t *)buffer, buf_len - 1, &out_len,
				       timeout_ms);

	if (out_len > 0) {
		buffer[out_len] = '\0';
	}

	return ret;
}

int wifi_uart_expect_response(wifi_uart_context_t *ctx, const char *expected,
			      uint32_t timeout_ms)
{
	if (!ctx || !expected) {
		return -1;
	}

	char response[512];
	int ret = wifi_uart_read_response(ctx, response, sizeof(response), timeout_ms);

	if (ret != 0) {
		return -1;
	}

	if (strstr(response, expected)) {
		return 0;
	}

	return -1;
}

int wifi_uart_poll(wifi_uart_context_t *ctx)
{
	if (!ctx) {
		return -1;
	}

	/* TODO: Poll UART for incoming data
	 * Check for unsolicited messages (e.g., connection state changes)
	 */

	return 0;
}

bool wifi_uart_is_connected(wifi_uart_context_t *ctx)
{
	if (!ctx) {
		return false;
	}
	return ctx->connected;
}

int wifi_uart_get_stats(wifi_uart_context_t *ctx, uint32_t *rx_count, uint32_t *tx_count,
		       uint32_t *errors)
{
	if (!ctx) {
		return -1;
	}

	if (rx_count) {
		*rx_count = ctx->rx_head;
	}
	if (tx_count) {
		*tx_count = ctx->tx_head;
	}
	if (errors) {
		*errors = 0; /* TODO: Track errors */
	}

	return 0;
}

/* ============================================================================
 * Coprocessor instantiation via UART
 * ============================================================================
 */

/* Operations bridge from coprocessor abstraction to UART transport */

static int wifi_uart_coproc_init(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->transport_data) {
		return -1;
	}

	wifi_uart_context_t *uart = (wifi_uart_context_t *)coproc->transport_data;

	/* Test connection with AT command */
	if (wifi_uart_send_command(uart, AT_CMD_TEST) != 0) {
		return -1;
	}

	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS) !=
	    0) {
		return -1;
	}

	/* Set station mode */
	if (wifi_uart_send_command(uart, AT_CMD_SET_MODE_STA) != 0) {
		return -1;
	}

	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS) !=
	    0) {
		return -1;
	}

	coproc->status = WIFI_STATUS_FIRMWARE_READY;
	uart->connected = true;

	return 0;
}

static int wifi_uart_coproc_deinit(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->transport_data) {
		return -1;
	}

	wifi_uart_context_t *uart = (wifi_uart_context_t *)coproc->transport_data;
	uart->connected = false;
	coproc->status = WIFI_STATUS_DOWN;

	return 0;
}

static wifi_coproc_status_t wifi_uart_coproc_get_status(wifi_coproc_t *coproc)
{
	if (!coproc) {
		return WIFI_STATUS_ERROR;
	}
	return coproc->status;
}

static int wifi_uart_coproc_start_scan(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->transport_data) {
		return -1;
	}

	wifi_uart_context_t *uart = (wifi_uart_context_t *)coproc->transport_data;

	coproc->status = WIFI_STATUS_SCANNING;

	if (wifi_uart_send_command(uart, AT_CMD_SCAN_START) != 0) {
		coproc->status = WIFI_STATUS_ERROR;
		return -1;
	}

	/* TODO: Read back scan results, parse SSID/RSSI/etc.
	 * For now, just mark as complete
	 */

	coproc->status = WIFI_STATUS_SCAN_COMPLETE;
	return 0;
}

static int wifi_uart_coproc_join_network(wifi_coproc_t *coproc,
					 const wifi_join_params_t *params)
{
	if (!coproc || !coproc->transport_data || !params) {
		return -1;
	}

	wifi_uart_context_t *uart = (wifi_uart_context_t *)coproc->transport_data;
	char cmd[256];

	coproc->status = WIFI_STATUS_AUTHENTICATING;

	/* Build AT+CWJAP command */
	snprintf(cmd, sizeof(cmd), AT_CMD_CONNECT, params->ssid, params->password);

	if (wifi_uart_send_command(uart, cmd) != 0) {
		coproc->status = WIFI_STATUS_ERROR;
		return -1;
	}

	/* Wait for connection response (may take several seconds) */
	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS * 3) !=
	    0) {
		coproc->status = WIFI_STATUS_ERROR;
		return -1;
	}

	coproc->status = WIFI_STATUS_CONNECTED;
	return 0;
}

static int wifi_uart_coproc_leave_network(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->transport_data) {
		return -1;
	}

	wifi_uart_context_t *uart = (wifi_uart_context_t *)coproc->transport_data;

	coproc->status = WIFI_STATUS_DISCONNECTING;

	if (wifi_uart_send_command(uart, AT_CMD_DISCONNECT) != 0) {
		return -1;
	}

	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS) !=
	    0) {
		return -1;
	}

	coproc->status = WIFI_STATUS_FIRMWARE_READY;
	return 0;
}

static int wifi_uart_coproc_send_packet(wifi_coproc_t *coproc, const uint8_t *data,
					size_t len)
{
	if (!coproc || !coproc->transport_data || !data) {
		return -1;
	}

	/* TODO: Implement packet send over UART
	 * Likely using AT+CIPSEND or similar command
	 */

	return 0;
}

static int wifi_uart_coproc_poll(wifi_coproc_t *coproc)
{
	if (!coproc || !coproc->transport_data) {
		return -1;
	}

	wifi_uart_context_t *uart = (wifi_uart_context_t *)coproc->transport_data;
	return wifi_uart_poll(uart);
}

static const wifi_coproc_ops_t wifi_uart_coproc_ops = {
	.init = wifi_uart_coproc_init,
	.deinit = wifi_uart_coproc_deinit,
	.reset = NULL,
	.get_status = wifi_uart_coproc_get_status,
	.set_mode = NULL,
	.start_scan = wifi_uart_coproc_start_scan,
	.get_scan_results = NULL,
	.join_network = wifi_uart_coproc_join_network,
	.leave_network = wifi_uart_coproc_leave_network,
	.get_connected_network = NULL,
	.send_packet = wifi_uart_coproc_send_packet,
	.receive_packet = NULL,
	.set_power_save = NULL,
	.poll = wifi_uart_coproc_poll,
};

int wifi_uart_coproc_create(const char *name, int uart_fd, wifi_uart_baud_t baud,
			    wifi_coproc_t **out_coproc)
{
	if (!name || uart_fd < 0 || !out_coproc) {
		return -1;
	}

	wifi_uart_context_t *uart_ctx = (wifi_uart_context_t *)malloc(
		sizeof(wifi_uart_context_t));
	if (!uart_ctx) {
		return -1;
	}

	if (wifi_uart_init(uart_ctx, uart_fd, baud) != 0) {
		free(uart_ctx);
		return -1;
	}

	wifi_coproc_t *coproc = NULL;
	if (wifi_coproc_create(name, &coproc) != 0) {
		free(uart_ctx);
		return -1;
	}

	wifi_coproc_register_transport(coproc, uart_ctx);
	wifi_coproc_register_ops(coproc, &wifi_uart_coproc_ops);

	*out_coproc = coproc;
	return 0;
}
