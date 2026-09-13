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
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define ASSERT(c)                                                              \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__,          \
				__LINE__, #c);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static void sim_reply(int fd, const char *msg)
{
	size_t n = strlen(msg);
	const char *p = msg;

	while (n > 0) {
		ssize_t w = write(fd, p, n);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		p += (size_t)w;
		n -= (size_t)w;
	}
}

static void esp_at_sim_loop(int fd)
{
	char buf[1024];
	size_t used = 0;

	for (;;) {
		ssize_t n;
		char *nl;

		n = read(fd, buf + used, sizeof(buf) - 1u - used);
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
			fprintf(stderr, "esp-at-sim: [%s]\n", buf);
			if (strcmp(buf, "AT") == 0) {
				sim_reply(fd, "\r\nOK\r\n");
			} else if (strcmp(buf, "AT+CWMODE=1") == 0) {
				sim_reply(fd, "\r\nOK\r\n");
			} else if (strcmp(buf, "AT+CWLAP") == 0) {
				sim_reply(fd,
					  "+CWLAP:(3,\"flinstone_ci\",-40,\"02:11:22:33:44:55\",6)\r\n");
				sim_reply(fd,
					  "+CWLAP:(4,\"LabWpa2\",-52,\"02:22:00:00:00:02\",11)\r\n");
				sim_reply(fd, "\r\nOK\r\n");
			} else if (strncmp(buf, "AT+CWJAP=", 9) == 0) {
				sim_reply(fd, "WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n");
			} else if (strcmp(buf, "AT+CWQAP") == 0) {
				sim_reply(fd, "\r\nOK\r\n");
			} else if (buf[0] != '\0') {
				sim_reply(fd, "\r\nERROR\r\n");
			}
			used -= (size_t)(nl + 1 - buf);
			memmove(buf, nl + 1, used + 1u);
		}
	}
}

int main(void)
{
	int sv[2];
	pid_t child;
	wifi_coproc_t *coproc = NULL;
	wifi_network_t nets[8];
	uint16_t n = 8;
	int saw_ci = 0;
	int rc = 1;
	size_t i;

	ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	child = fork();
	ASSERT(child >= 0);
	if (child == 0) {
		(void)close(sv[0]);
		esp_at_sim_loop(sv[1]);
		(void)close(sv[1]);
		_exit(0);
	}
	(void)close(sv[1]);

	if (wifi_uart_coproc_create("wlan0", sv[0], WIFI_UART_BAUD_115200, &coproc) != 0) {
		fprintf(stderr, "FAIL wifi_uart_coproc_create fd=%d host_fd=%d\n", sv[0],
			wifi_platform_host_uart_fd());
		goto done;
	}
	if (wifi_coproc_init(coproc) != 0) {
		int st = 0;

		fprintf(stderr, "FAIL wifi_coproc_init host_fd=%d\n",
			wifi_platform_host_uart_fd());
		if (waitpid(child, &st, WNOHANG) == child)
			fprintf(stderr, "esp-at-sim child exited status=%d\n", st);
		goto done;
	}
	if (wifi_coproc_get_status(coproc) != WIFI_STATUS_FIRMWARE_READY) {
		fprintf(stderr, "FAIL status after init=%d\n",
			(int)wifi_coproc_get_status(coproc));
		goto done;
	}
	printf("ok #328 uart AT ready (AT + AT+CWMODE=1)\n");

	if (wifi_coproc_scan(coproc) != 0) {
		fprintf(stderr, "FAIL wifi_coproc_scan\n");
		goto done;
	}
	if (wifi_coproc_get_scan_results(coproc, nets, &n) != 0 || n < 1u) {
		fprintf(stderr, "FAIL scan results n=%u\n", (unsigned)n);
		goto done;
	}
	for (i = 0; i < n; i++) {
		printf("wifi scan: ssid=%s rssi=%d ch=%u auth=%u\n", nets[i].ssid,
		       (int)nets[i].rssi, (unsigned)nets[i].channel,
		       (unsigned)nets[i].auth_mode);
		if (strcmp(nets[i].ssid, "flinstone_ci") == 0)
			saw_ci = 1;
	}
	if (!saw_ci) {
		fprintf(stderr, "FAIL missing flinstone_ci in AT+CWLAP\n");
		goto done;
	}
	printf("ok #328 wifi scan (AT+CWLAP) count=%u\n", (unsigned)n);

	if (wifi_coproc_join(coproc, "flinstone_ci", "flinstone_test_psk",
			     WIFI_AUTH_WPA2_PSK) != 0) {
		fprintf(stderr, "FAIL wifi_coproc_join\n");
		goto done;
	}
	if (wifi_coproc_get_status(coproc) != WIFI_STATUS_CONNECTED) {
		fprintf(stderr, "FAIL join status=%d\n",
			(int)wifi_coproc_get_status(coproc));
		goto done;
	}
	printf("wifi join: CONNECTED ssid=flinstone_ci (AT+CWJAP, no OS supplicant)\n");
	printf("ok #328 wifi join over UART AT\n");

	if (wifi_coproc_disconnect(coproc) != 0) {
		fprintf(stderr, "FAIL wifi_coproc_disconnect\n");
		goto done;
	}
	rc = 0;
	printf("test_wifi_uart_at_scan_join: passed\n");

done:
	if (coproc)
		wifi_coproc_destroy(coproc);
	(void)wifi_platform_host_uart_bind(-1);
	(void)shutdown(sv[0], SHUT_RDWR);
	(void)close(sv[0]);
	(void)kill(child, SIGTERM);
	(void)waitpid(child, NULL, 0);
	return rc;
}
