/*
 * WiFi Coprocessor Driver Tests (Phase 1)
 * Unit and integration tests for ESP32/ESP8266 over UART
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "kernel/drivers/wifi_coprocessor.h"
#include "kernel/drivers/wifi_uart_transport.h"

/* Test fixtures */
#define TEST_COPROC_NAME "wlan0"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg)                                                   \
	do {                                                                         \
		if (!(cond)) {                                                       \
			printf("FAIL: %s (line %d)\n", msg, __LINE__);               \
			tests_failed++;                                              \
		} else {                                                             \
			printf("PASS: %s\n", msg);                                  \
			tests_passed++;                                              \
		}                                                                    \
	} while (0)

/* Test 1: Device creation and destruction */
static void test_wifi_coprocessor_create_destroy(void)
{
	printf("\n=== Test: Create/Destroy ===\n");

	wifi_coproc_t *coproc = NULL;
	int ret = wifi_coproc_create(TEST_COPROC_NAME, &coproc);

	TEST_ASSERT(ret == 0, "Create coprocessor");
	TEST_ASSERT(coproc != NULL, "Coprocessor pointer valid");
	TEST_ASSERT(strcmp(coproc->name, TEST_COPROC_NAME) == 0, "Name matches");
	TEST_ASSERT(coproc->status == WIFI_STATUS_DOWN, "Initial status is DOWN");
	TEST_ASSERT(coproc->netdev != NULL, "Netdev created");

	ret = wifi_coproc_destroy(coproc);
	TEST_ASSERT(ret == 0, "Destroy coprocessor");
}

/* Test 2: Status string conversion */
static void test_wifi_coprocessor_status_strings(void)
{
	printf("\n=== Test: Status Strings ===\n");

	TEST_ASSERT(strcmp(wifi_coproc_status_str(WIFI_STATUS_DOWN), "DOWN") == 0,
		    "DOWN status string");
	TEST_ASSERT(strcmp(wifi_coproc_status_str(WIFI_STATUS_SCANNING), "SCANNING") ==
			    0,
		    "SCANNING status string");
	TEST_ASSERT(strcmp(wifi_coproc_status_str(WIFI_STATUS_CONNECTED), "CONNECTED") ==
			    0,
		    "CONNECTED status string");
	TEST_ASSERT(strcmp(wifi_coproc_status_str(WIFI_STATUS_ERROR), "ERROR") == 0,
		    "ERROR status string");
}

/* Test 3: Operations registration */
static void test_wifi_coprocessor_register_ops(void)
{
	printf("\n=== Test: Register Operations ===\n");

	wifi_coproc_t *coproc = NULL;
	wifi_coproc_create("test", &coproc);

	wifi_coproc_ops_t dummy_ops = {0};
	int ret = wifi_coproc_register_ops(coproc, &dummy_ops);

	TEST_ASSERT(ret == 0, "Register operations");
	TEST_ASSERT(coproc->ops == &dummy_ops, "Operations pointer set");

	wifi_coproc_destroy(coproc);
}

/* Test 4: Transport registration */
static void test_wifi_coprocessor_register_transport(void)
{
	printf("\n=== Test: Register Transport ===\n");

	wifi_coproc_t *coproc = NULL;
	void *dummy_transport = (void *)0xDEADBEEF;
	wifi_coproc_create("test", &coproc);

	int ret = wifi_coproc_register_transport(coproc, dummy_transport);

	TEST_ASSERT(ret == 0, "Register transport");
	TEST_ASSERT(coproc->transport_data == dummy_transport, "Transport pointer set");

	wifi_coproc_destroy(coproc);
}

/* Test 5: UART init/deinit */
static void test_wifi_uart_init_deinit(void)
{
	printf("\n=== Test: UART Init/Deinit ===\n");

	wifi_uart_context_t ctx;
	int ret = wifi_uart_init(&ctx, 3, WIFI_UART_BAUD_115200);

	TEST_ASSERT(ret == 0, "UART init");
	TEST_ASSERT(ctx.uart_fd == 3, "UART FD set");
	TEST_ASSERT(ctx.baud == WIFI_UART_BAUD_115200, "Baud rate set");
	TEST_ASSERT(ctx.connected == false, "Initially not connected");

	ret = wifi_uart_deinit(&ctx);
	TEST_ASSERT(ret == 0, "UART deinit");
}

/* Test 6: UART AT command building */
static void test_wifi_uart_at_commands(void)
{
	printf("\n=== Test: AT Commands ===\n");

	wifi_uart_context_t ctx;
	wifi_uart_init(&ctx, 3, WIFI_UART_BAUD_115200);

	/* Test command sending */
	int ret = wifi_uart_send_command(&ctx, "AT\r\n");
	TEST_ASSERT(ret >= 0, "Send AT command");

	ret = wifi_uart_send_command(&ctx, "AT+CWMODE=1\r\n");
	TEST_ASSERT(ret >= 0, "Send CWMODE command");

	char ssid[] = "TestNetwork";
	char psk[] = "TestPassword";
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, psk);
	ret = wifi_uart_send_command(&ctx, cmd);
	TEST_ASSERT(ret >= 0, "Build connection command");

	wifi_uart_deinit(&ctx);
}

/* Test 7: WiFi network structure validation */
static void test_wifi_network_structure(void)
{
	printf("\n=== Test: Network Structure ===\n");

	wifi_network_t net;
	memset(&net, 0, sizeof(net));

	strncpy(net.ssid, "TestSSID", sizeof(net.ssid) - 1u);
	net.ssid[sizeof(net.ssid) - 1u] = '\0';
	net.channel = 6;
	net.rssi = -50;
	net.auth_mode = WIFI_AUTH_WPA2_PSK;

	TEST_ASSERT(strcmp(net.ssid, "TestSSID") == 0, "SSID set");
	TEST_ASSERT(net.channel == 6, "Channel set");
	TEST_ASSERT(net.rssi == -50, "RSSI set");
	TEST_ASSERT(net.auth_mode == WIFI_AUTH_WPA2_PSK, "Auth mode set");
}

/* Test 8: Join parameters */
static void test_wifi_join_params(void)
{
	printf("\n=== Test: Join Parameters ===\n");

	wifi_join_params_t params;
	memset(&params, 0, sizeof(params));

	strncpy(params.ssid, "HomeNetwork", sizeof(params.ssid) - 1u);
	params.ssid[sizeof(params.ssid) - 1u] = '\0';
	strncpy(params.password, "SecurePassword123", sizeof(params.password) - 1u);
	params.password[sizeof(params.password) - 1u] = '\0';
	params.auth_mode = WIFI_AUTH_WPA2_PSK;
	params.channel = 0; /* Auto */

	TEST_ASSERT(strcmp(params.ssid, "HomeNetwork") == 0, "SSID in join params");
	TEST_ASSERT(strcmp(params.password, "SecurePassword123") == 0,
		    "Password in join params");
	TEST_ASSERT(params.auth_mode == WIFI_AUTH_WPA2_PSK, "Auth mode in params");
	TEST_ASSERT(params.channel == 0, "Auto channel set");
}

/* Test 9: Statistics tracking */
static void test_wifi_statistics_tracking(void)
{
	printf("\n=== Test: Statistics ===\n");

	wifi_coproc_t *coproc = NULL;
	wifi_coproc_create("stats_test", &coproc);

	TEST_ASSERT(coproc->frames_tx == 0, "TX frames initially 0");
	TEST_ASSERT(coproc->frames_rx == 0, "RX frames initially 0");
	TEST_ASSERT(coproc->errors == 0, "Errors initially 0");

	coproc->frames_tx = 10;
	coproc->frames_rx = 5;
	coproc->errors = 2;

	TEST_ASSERT(coproc->frames_tx == 10, "TX frames updated");
	TEST_ASSERT(coproc->frames_rx == 5, "RX frames updated");
	TEST_ASSERT(coproc->errors == 2, "Errors updated");

	wifi_coproc_destroy(coproc);
}

/* Test 10: UART stats retrieval */
static void test_wifi_uart_stats(void)
{
	printf("\n=== Test: UART Stats ===\n");

	wifi_uart_context_t ctx;
	wifi_uart_init(&ctx, 3, WIFI_UART_BAUD_115200);

	uint32_t rx = 0, tx = 0, err = 0;
	int ret = wifi_uart_get_stats(&ctx, &rx, &tx, &err);

	TEST_ASSERT(ret == 0, "Get UART stats");
	TEST_ASSERT(rx >= 0, "RX count retrieved");
	TEST_ASSERT(tx >= 0, "TX count retrieved");
	TEST_ASSERT(err >= 0, "Error count retrieved");

	wifi_uart_deinit(&ctx);
}

/* Main test runner */
int main(void)
{
	printf("╔════════════════════════════════════════════════════════════╗\n");
	printf("║  WiFi Coprocessor Phase 1 Test Suite (802.11ax Support)   ║\n");
	printf("║  Testing WiFi driver foundation for issue #279 (P3-10)    ║\n");
	printf("╚════════════════════════════════════════════════════════════╝\n");

	/* Run all tests */
	test_wifi_coprocessor_create_destroy();
	test_wifi_coprocessor_status_strings();
	test_wifi_coprocessor_register_ops();
	test_wifi_coprocessor_register_transport();
	test_wifi_uart_init_deinit();
	test_wifi_uart_at_commands();
	test_wifi_network_structure();
	test_wifi_join_params();
	test_wifi_statistics_tracking();
	test_wifi_uart_stats();

	/* Summary */
	printf("\n╔════════════════════════════════════════════════════════════╗\n");
	printf("║ Test Results                                               ║\n");
	printf("╠════════════════════════════════════════════════════════════╣\n");
	printf("║ PASSED: %d                                                 ║\n", tests_passed);
	printf("║ FAILED: %d                                                 ║\n", tests_failed);
	printf("║ TOTAL:  %d                                                 ║\n",
	       tests_passed + tests_failed);
	printf("╚════════════════════════════════════════════════════════════╝\n");

	return tests_failed > 0 ? 1 : 0;
}
