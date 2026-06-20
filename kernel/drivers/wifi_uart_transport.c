/*
 * WiFi Coprocessor UART Transport Implementation
 * Handles serial communication with ESP32/ESP8266 WiFi modules
 * Integrates with platform UART abstraction for cross-platform support
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "kernel/drivers/wifi_uart_transport.h"
#include "kernel/drivers/wifi_platform.h"
#include "kernel/drivers/wifi_driver_packet.h"
#include "kernel/core/net/net_wire.h"

#include "fl/mm.h"
#include "fl/mem_asm.h"

/* AT Command strings for ESP32/ESP8266 */
#define AT_CMD_TEST          "AT\r\n"
#define AT_CMD_SET_MODE_STA  "AT+CWMODE=1\r\n"
#define AT_CMD_SCAN_START    "AT+CWLAP\r\n"
#define AT_CMD_CONNECT       "AT+CWJAP=\"%s\",\"%s\"\r\n"
#define AT_CMD_DISCONNECT    "AT+CWQAP\r\n"
#define AT_CMD_CIPSEND       "AT+CIPSEND=%zu\r\n"

/* Response markers */
#define AT_RESP_OK      "OK"
#define AT_RESP_ERROR   "ERROR"
#define AT_RESP_FAIL    "FAIL"

static wifi_auth_mode_t wifi_uart_ecn_to_auth(int ecn)
{
	switch (ecn) {
	case 0:
		return WIFI_AUTH_OPEN;
	case 1:
		return WIFI_AUTH_WEP;
	case 2:
		return WIFI_AUTH_WPA_PSK;
	case 3:
		return WIFI_AUTH_WPA2_PSK;
	case 4:
		return WIFI_AUTH_WPA2_PSK;
	default:
		return WIFI_AUTH_UNKNOWN;
	}
}

static int wifi_uart_parse_mac(const char *mac_str, uint8_t bssid[WIFI_BSSID_LEN])
{
	unsigned a, b, c, d, e, f;

	if (!mac_str || !bssid)
		return -1;
	if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x", &a, &b, &c, &d, &e, &f) != 6)
		return -1;
	bssid[0] = (uint8_t)a;
	bssid[1] = (uint8_t)b;
	bssid[2] = (uint8_t)c;
	bssid[3] = (uint8_t)d;
	bssid[4] = (uint8_t)e;
	bssid[5] = (uint8_t)f;
	return 0;
}

static int wifi_uart_parse_cwlap_line(const char *line, wifi_network_t *out)
{
	int ecn = 0;
	int rssi = 0;
	int channel = 0;
	char ssid[WIFI_SSID_MAX + 1];
	char mac[18];

	if (!line || !out)
		return -1;
	if (strncmp(line, "+CWLAP:", 7) != 0)
		return -1;

	if (sscanf(line + 7, "(%d,\"%32[^\"]\",%d,\"%17[^\"]\",%d)", &ecn, ssid, &rssi,
		   mac, &channel) < 5)
		return -1;

	asm_mem_zero(out, sizeof(*out));
	strncpy(out->ssid, ssid, WIFI_SSID_MAX);
	out->ssid[WIFI_SSID_MAX] = '\0';
	out->rssi = (int8_t)rssi;
	out->channel = (uint8_t)channel;
	out->auth_mode = wifi_uart_ecn_to_auth(ecn);
	if (wifi_uart_parse_mac(mac, out->bssid) != 0)
		asm_mem_zero(out->bssid, sizeof(out->bssid));
	return 0;
}

static int wifi_uart_parse_scan_response(wifi_uart_context_t *uart, const char *response)
{
	const char *line;
	size_t count = 0;

	if (!uart || !response)
		return -1;

	uart->scan_count = 0;
	asm_mem_zero(uart->scan_results, sizeof(uart->scan_results));

	for (line = response; *line && count < WIFI_MAX_NETWORKS; line++) {
		if (strncmp(line, "+CWLAP:", 7) == 0) {
			if (wifi_uart_parse_cwlap_line(line, &uart->scan_results[count]) == 0)
				count++;
			while (*line && *line != '\n')
				line++;
		}
	}

	uart->scan_count = (uint16_t)count;
	return count > 0 ? 0 : -1;
}

int wifi_uart_init(wifi_uart_context_t *ctx, int uart_fd, wifi_uart_baud_t baud)
{
	if (!ctx || uart_fd < 0)
		return -1;

	asm_mem_zero(ctx, sizeof(*ctx));
	ctx->uart_fd = uart_fd;
	ctx->baud = baud;
	ctx->parity = WIFI_UART_PARITY_NONE;
	ctx->connected = false;
	return 0;
}

int wifi_uart_deinit(wifi_uart_context_t *ctx)
{
	if (!ctx)
		return -1;

	asm_mem_zero(ctx, sizeof(*ctx));
	return 0;
}

int wifi_uart_send_raw(wifi_uart_context_t *ctx, const uint8_t *data, size_t len)
{
	const wifi_platform_uart_ops_t *uart_ops;

	if (!ctx || !data || len == 0)
		return -1;

	uart_ops = wifi_platform_get_uart_ops();
	if (!uart_ops || !uart_ops->write_bytes)
		return -1;

	if (uart_ops->write_bytes(data, len) != 0) {
		ctx->error_count++;
		return -1;
	}

	if (uart_ops->flush)
		uart_ops->flush();

	ctx->tx_head += len;
	ctx->last_command_ms = wifi_platform_get_ms();
	return (int)len;
}

int wifi_uart_receive_raw(wifi_uart_context_t *ctx, uint8_t *buffer, size_t buf_len,
			  size_t *out_len, uint32_t timeout_ms)
{
	const wifi_platform_uart_ops_t *uart_ops;
	int ret;

	if (!ctx || !buffer || buf_len == 0 || !out_len)
		return -1;

	uart_ops = wifi_platform_get_uart_ops();
	if (!uart_ops || !uart_ops->read_bytes)
		return -1;

	ret = uart_ops->read_bytes(buffer, buf_len, out_len, timeout_ms);

	if (*out_len > 0) {
		ctx->rx_head += *out_len;
		ctx->last_response_ms = wifi_platform_get_ms();
	} else if (ret != 0) {
		ctx->error_count++;
	}

	return ret;
}

int wifi_uart_send_command(wifi_uart_context_t *ctx, const char *cmd)
{
	if (!ctx || !cmd)
		return -1;
	return wifi_uart_send_raw(ctx, (const uint8_t *)cmd, strlen(cmd));
}

int wifi_uart_read_response(wifi_uart_context_t *ctx, char *buffer, size_t buf_len,
			    uint32_t timeout_ms)
{
	size_t total_read = 0;
	uint32_t start_time;

	if (!ctx || !buffer || buf_len == 0)
		return -1;

	start_time = wifi_platform_get_ms();

	while (total_read < buf_len - 1) {
		size_t chunk_len = 0;
		uint32_t elapsed = wifi_platform_get_ms() - start_time;
		uint32_t remaining_timeout;
		int ret;

		if (elapsed >= timeout_ms)
			break;

		remaining_timeout = timeout_ms - elapsed;
		ret = wifi_uart_receive_raw(ctx, (uint8_t *)&buffer[total_read],
					    buf_len - 1 - total_read, &chunk_len,
					    remaining_timeout);

		if (chunk_len > 0) {
			total_read += chunk_len;
			if (strstr(&buffer[total_read > 10 ? total_read - 10 : 0], "\r\nOK\r\n") ||
			    strstr(&buffer[total_read > 10 ? total_read - 10 : 0],
				   "\r\nERROR\r\n") ||
			    strstr(&buffer[total_read > 10 ? total_read - 10 : 0],
				   "\r\nFAIL\r\n"))
				break;
		} else if (ret != 0) {
			wifi_platform_sleep_ms(10);
		}
	}

	buffer[total_read] = '\0';
	return total_read > 0 ? 0 : -1;
}

int wifi_uart_expect_response(wifi_uart_context_t *ctx, const char *expected,
			      uint32_t timeout_ms)
{
	char response[512];

	if (!ctx || !expected)
		return -1;

	if (wifi_uart_read_response(ctx, response, sizeof(response), timeout_ms) != 0)
		return -1;

	if (strstr(response, expected))
		return 0;

	ctx->error_count++;
	return -1;
}

int wifi_uart_poll(wifi_uart_context_t *ctx)
{
	const wifi_platform_uart_ops_t *uart_ops;
	uint8_t byte;

	if (!ctx)
		return -1;

	uart_ops = wifi_platform_get_uart_ops();
	if (!uart_ops || !uart_ops->read_byte)
		return 0;

	while (uart_ops->read_byte(&byte, 0) == 0) {
		if (ctx->rx_staging_len < sizeof(ctx->rx_staging)) {
			ctx->rx_staging[ctx->rx_staging_len++] = byte;
			ctx->rx_head++;
		} else {
			ctx->error_count++;
			break;
		}
	}

	return 0;
}

bool wifi_uart_is_connected(wifi_uart_context_t *ctx)
{
	if (!ctx)
		return false;
	return ctx->connected;
}

int wifi_uart_get_stats(wifi_uart_context_t *ctx, uint32_t *rx_count, uint32_t *tx_count,
			uint32_t *errors)
{
	if (!ctx)
		return -1;

	if (rx_count)
		*rx_count = ctx->rx_head;
	if (tx_count)
		*tx_count = ctx->tx_head;
	if (errors)
		*errors = ctx->error_count;
	return 0;
}

static int wifi_uart_coproc_init(wifi_coproc_t *coproc)
{
	wifi_uart_context_t *uart;

	if (!coproc || !coproc->transport_data)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;

	if (wifi_uart_send_command(uart, AT_CMD_TEST) != 0)
		return -1;
	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS) != 0)
		return -1;

	if (wifi_uart_send_command(uart, AT_CMD_SET_MODE_STA) != 0)
		return -1;
	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS) != 0)
		return -1;

	coproc->status = WIFI_STATUS_FIRMWARE_READY;
	uart->connected = true;
	return 0;
}

static int wifi_uart_coproc_deinit(wifi_coproc_t *coproc)
{
	wifi_uart_context_t *uart;

	if (!coproc || !coproc->transport_data)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;
	uart->connected = false;
	coproc->status = WIFI_STATUS_DOWN;
	return 0;
}

static wifi_coproc_status_t wifi_uart_coproc_get_status(wifi_coproc_t *coproc)
{
	if (!coproc)
		return WIFI_STATUS_ERROR;
	return coproc->status;
}

static int wifi_uart_coproc_start_scan(wifi_coproc_t *coproc)
{
	wifi_uart_context_t *uart;
	char response[4096];

	if (!coproc || !coproc->transport_data)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;
	coproc->status = WIFI_STATUS_SCANNING;

	if (wifi_uart_send_command(uart, AT_CMD_SCAN_START) != 0) {
		coproc->status = WIFI_STATUS_ERROR;
		uart->error_count++;
		return -1;
	}

	if (wifi_uart_read_response(uart, response, sizeof(response),
				    WIFI_UART_CMD_TIMEOUT_MS * 2) != 0) {
		coproc->status = WIFI_STATUS_ERROR;
		uart->error_count++;
		return -1;
	}

	(void)wifi_uart_parse_scan_response(uart, response);
	coproc->status = WIFI_STATUS_SCAN_COMPLETE;
	return 0;
}

static int wifi_uart_coproc_get_scan_results(wifi_coproc_t *coproc,
					     wifi_network_t *networks, uint16_t *count)
{
	wifi_uart_context_t *uart;
	uint16_t i;
	uint16_t max;

	if (!coproc || !coproc->transport_data || !networks || !count)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;
	max = *count;
	if (max > uart->scan_count)
		max = uart->scan_count;

	for (i = 0; i < max; i++)
		networks[i] = uart->scan_results[i];
	*count = max;
	return max > 0 ? 0 : -1;
}

static int wifi_uart_coproc_join_network(wifi_coproc_t *coproc,
					 const wifi_join_params_t *params)
{
	wifi_uart_context_t *uart;
	char cmd[256];

	if (!coproc || !coproc->transport_data || !params)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;
	coproc->status = WIFI_STATUS_AUTHENTICATING;

	snprintf(cmd, sizeof(cmd), AT_CMD_CONNECT, params->ssid, params->password);
	if (wifi_uart_send_command(uart, cmd) != 0) {
		coproc->status = WIFI_STATUS_ERROR;
		uart->error_count++;
		return -1;
	}

	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS * 3) != 0) {
		coproc->status = WIFI_STATUS_ERROR;
		return -1;
	}

	coproc->status = WIFI_STATUS_CONNECTED;
	return 0;
}

static int wifi_uart_coproc_leave_network(wifi_coproc_t *coproc)
{
	wifi_uart_context_t *uart;

	if (!coproc || !coproc->transport_data)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;
	coproc->status = WIFI_STATUS_DISCONNECTING;

	if (wifi_uart_send_command(uart, AT_CMD_DISCONNECT) != 0)
		return -1;
	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS) != 0)
		return -1;

	coproc->status = WIFI_STATUS_FIRMWARE_READY;
	return 0;
}

static int wifi_uart_coproc_send_packet(wifi_coproc_t *coproc, const uint8_t *data,
					size_t len)
{
	wifi_uart_context_t *uart;
	char cmd[64];

	if (!coproc || !coproc->transport_data || !data || len == 0)
		return -1;

	if (wifi_driver_packet_validate_tx(data, len) != FL_RESULT_OK)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;

	/*
	 * ESP AT firmware expects AT+CIPSEND before raw payload bytes.
	 * Until transparent socket mode is wired, fail closed (no silent drop).
	 */
	snprintf(cmd, sizeof(cmd), AT_CMD_CIPSEND, len);
	if (wifi_uart_send_command(uart, cmd) != 0) {
		uart->error_count++;
		return -1;
	}
	if (wifi_uart_expect_response(uart, ">", WIFI_UART_TX_TIMEOUT_MS) != 0) {
		uart->error_count++;
		return -1;
	}
	if (wifi_uart_send_raw(uart, data, len) != (int)len) {
		uart->error_count++;
		return -1;
	}
	if (wifi_uart_expect_response(uart, AT_RESP_OK, WIFI_UART_CMD_TIMEOUT_MS) != 0) {
		uart->error_count++;
		return -1;
	}

	return 0;
}

static int wifi_uart_coproc_receive_packet(wifi_coproc_t *coproc, uint8_t *buffer,
					   size_t buf_len, size_t *out_len)
{
	wifi_uart_context_t *uart;
	size_t n;

	if (!coproc || !coproc->transport_data || !buffer || !out_len)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;
	(void)wifi_uart_poll(uart);

	if (uart->rx_staging_len == 0)
		return -1;

	n = uart->rx_staging_len;
	if (n > buf_len)
		n = buf_len;

	asm_mem_copy(buffer, uart->rx_staging, n);
	*out_len = n;

	if (wifi_driver_packet_validate_tx(buffer, n) != FL_RESULT_OK) {
		uart->error_count++;
		uart->rx_staging_len = 0;
		return -1;
	}

	uart->rx_staging_len = 0;
	return 0;
}

static int wifi_uart_coproc_poll(wifi_coproc_t *coproc)
{
	wifi_uart_context_t *uart;

	if (!coproc || !coproc->transport_data)
		return -1;

	uart = (wifi_uart_context_t *)coproc->transport_data;
	return wifi_uart_poll(uart);
}

static const wifi_coproc_ops_t wifi_uart_coproc_ops = {
	.init = wifi_uart_coproc_init,
	.deinit = wifi_uart_coproc_deinit,
	.reset = NULL,
	.get_status = wifi_uart_coproc_get_status,
	.set_mode = NULL,
	.start_scan = wifi_uart_coproc_start_scan,
	.get_scan_results = wifi_uart_coproc_get_scan_results,
	.join_network = wifi_uart_coproc_join_network,
	.leave_network = wifi_uart_coproc_leave_network,
	.get_connected_network = NULL,
	.send_packet = wifi_uart_coproc_send_packet,
	.receive_packet = wifi_uart_coproc_receive_packet,
	.set_power_save = NULL,
	.poll = wifi_uart_coproc_poll,
};

int wifi_uart_coproc_create(const char *name, int uart_fd, wifi_uart_baud_t baud,
			    wifi_coproc_t **out_coproc)
{
	wifi_uart_context_t *uart_ctx;
	wifi_coproc_t *coproc;

	if (!name || uart_fd < 0 || !out_coproc)
		return -1;

	uart_ctx = (wifi_uart_context_t *)kmalloc(sizeof(wifi_uart_context_t));
	if (!uart_ctx)
		return -1;

	if (wifi_uart_init(uart_ctx, uart_fd, baud) != 0) {
		kfree(uart_ctx);
		return -1;
	}

	if (wifi_coproc_create(name, &coproc) != 0) {
		kfree(uart_ctx);
		return -1;
	}

	wifi_coproc_register_transport(coproc, uart_ctx);
	wifi_coproc_register_ops(coproc, &wifi_uart_coproc_ops);

	*out_coproc = coproc;
	return 0;
}
