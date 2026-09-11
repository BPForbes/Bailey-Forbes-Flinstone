/*
 * #328 item 2 — ESP32/ESP8266 UART AT path: wifi scan + wifi join.
 *
 * Speaks AT over a socketpair against the in-tree coprocessor driver
 * (same commands as a physical ESP AT firmware: AT, AT+CWMODE=1,
 * AT+CWLAP, AT+CWJAP). Does not use wpa_cli / nmcli / NetworkManager.
 */
#define _GNU_SOURCE
#include "wifi_coprocessor.h"
#include "wifi_uart_transport.h"
#include "wifi_platform.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define ASSERT(c)                                                              \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__,          \
				__LINE__, #c);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static volatile int s_sim_run;
static int s_sim_fd = -1;

static void sim_reply(const char *msg)
{
	size_t n = strlen(msg);
	const char *p = msg;

	while (n > 0) {
		ssize_t w = write(s_sim_fd, p, n);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		p += (size_t)w;
		n -= (size_t)w;
	}
}

static void *esp_at_sim_thread(void *arg)
{
	char buf[1024];
	size_t used = 0;

	(void)arg;
	while (s_sim_run) {
		ssize_t n;
		char *nl;

		n = read(s_sim_fd, buf + used, sizeof(buf) - 1u - used);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;
		used += (size_t)n;
		buf[used] = '\0';
		while ((nl = strchr(buf, '\n')) != NULL) {
			*nl = '\0';
			if (nl > buf && nl[-1] == '\r')
				nl[-1] = '\0';
			if (strcmp(buf, "AT") == 0) {
				sim_reply("\r\nOK\r\n");
			} else if (strcmp(buf, "AT+CWMODE=1") == 0) {
				sim_reply("\r\nOK\r\n");
			} else if (strcmp(buf, "AT+CWLAP") == 0) {
				sim_reply("+CWLAP:(3,\"flinstone_ci\",-40,\"02:11:22:33:44:55\",6)\r\n");
				sim_reply("+CWLAP:(4,\"LabWpa2\",-52,\"02:22:00:00:00:02\",11)\r\n");
				sim_reply("\r\nOK\r\n");
			} else if (strncmp(buf, "AT+CWJAP=", 9) == 0) {
				sim_reply("WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n");
			} else if (strcmp(buf, "AT+CWQAP") == 0) {
				sim_reply("\r\nOK\r\n");
			} else if (buf[0] != '\0') {
				sim_reply("\r\nERROR\r\n");
			}
			used -= (size_t)(nl + 1 - buf);
			memmove(buf, nl + 1, used + 1u);
		}
	}
	return NULL;
}

int main(void)
{
	int sv[2];
	pthread_t thr;
	wifi_coproc_t *coproc = NULL;
	wifi_network_t nets[8];
	uint16_t n = 8;
	int saw_ci = 0;
	size_t i;

	ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	s_sim_fd = sv[1];
	s_sim_run = 1;
	ASSERT(pthread_create(&thr, NULL, esp_at_sim_thread, NULL) == 0);

	ASSERT(wifi_uart_coproc_create("wlan0", sv[0], WIFI_UART_BAUD_115200,
				       &coproc) == 0);
	ASSERT(coproc != NULL);
	ASSERT(wifi_coproc_init(coproc) == 0);
	ASSERT(wifi_coproc_get_status(coproc) == WIFI_STATUS_FIRMWARE_READY);
	printf("ok #328 uart AT ready (AT + AT+CWMODE=1)\n");

	ASSERT(wifi_coproc_scan(coproc) == 0);
	ASSERT(wifi_coproc_get_scan_results(coproc, nets, &n) == 0);
	ASSERT(n >= 1u);
	for (i = 0; i < n; i++) {
		printf("wifi scan: ssid=%s rssi=%d ch=%u auth=%u\n", nets[i].ssid,
		       (int)nets[i].rssi, (unsigned)nets[i].channel,
		       (unsigned)nets[i].auth_mode);
		if (strcmp(nets[i].ssid, "flinstone_ci") == 0)
			saw_ci = 1;
	}
	ASSERT(saw_ci);
	printf("ok #328 wifi scan (AT+CWLAP) count=%u\n", (unsigned)n);

	ASSERT(wifi_coproc_join(coproc, "flinstone_ci", "flinstone_test_psk",
				WIFI_AUTH_WPA2_PSK) == 0);
	ASSERT(wifi_coproc_get_status(coproc) == WIFI_STATUS_CONNECTED);
	printf("wifi join: CONNECTED ssid=flinstone_ci (AT+CWJAP, no OS supplicant)\n");
	printf("ok #328 wifi join over UART AT\n");

	ASSERT(wifi_coproc_disconnect(coproc) == 0);
	wifi_coproc_destroy(coproc);
	(void)wifi_platform_host_uart_bind(-1);
	s_sim_run = 0;
	(void)shutdown(sv[0], SHUT_RDWR);
	(void)close(sv[0]);
	(void)close(sv[1]);
	(void)pthread_join(thr, NULL);
	printf("test_wifi_uart_at_scan_join: passed\n");
	return 0;
}
