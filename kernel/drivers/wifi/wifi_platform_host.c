/*
 * WiFi Platform Implementation (hosted x86/x64)
 * POSIX UART bind for ESP AT PTY / socketpair tests; stub I/O when unbound.
 */

#include "wifi_platform.h"
#include "kernel/core/time/timekeeping.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static int s_host_uart_fd = -1;

static speed_t host_uart_baud_to_speed(unsigned baud)
{
	switch (baud) {
	case 9600u:
		return B9600;
	case 19200u:
		return B19200;
	case 38400u:
		return B38400;
	case 57600u:
		return B57600;
	case 230400u:
		return B230400;
#ifdef B460800
	case 460800u:
		return B460800;
#endif
#ifdef B921600
	case 921600u:
		return B921600;
#endif
	case 115200u:
	default:
		return B115200;
	}
}

/*
 * Raw 8N1 on real ttys and PTYs so AT framing is not cooked/echoed.
 * Socketpairs are not ttys — leave them unchanged for unit tests.
 */
static int host_uart_configure_tty(int fd, unsigned baud)
{
	struct termios tio;
	speed_t speed;

	if (fd < 0 || !isatty(fd))
		return 0;
	if (tcgetattr(fd, &tio) != 0)
		return -1;

	cfmakeraw(&tio);
	tio.c_cflag |= (tcflag_t)(CLOCAL | CREAD);
	tio.c_cflag &= ~(tcflag_t)(PARENB | CSTOPB | CSIZE);
	tio.c_cflag |= (tcflag_t)CS8;
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;

	if (baud == 0u)
		baud = 115200u;
	speed = host_uart_baud_to_speed(baud);
	if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0)
		return -1;
	if (tcsetattr(fd, TCSANOW, &tio) != 0)
		return -1;
	(void)tcflush(fd, TCIOFLUSH);
	return 0;
}

int wifi_platform_host_uart_bind(int fd)
{
	s_host_uart_fd = fd;
	return 0;
}

int wifi_platform_host_uart_configure(unsigned baud)
{
	if (s_host_uart_fd < 0)
		return 0;
	return host_uart_configure_tty(s_host_uart_fd, baud);
}

int wifi_platform_host_uart_fd(void)
{
	return s_host_uart_fd;
}

static int host_uart_poll_in(uint32_t timeout_ms)
{
	struct pollfd pfd;
	int rc;

	if (s_host_uart_fd < 0)
		return -1;
	pfd.fd = s_host_uart_fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	rc = poll(&pfd, 1, (int)timeout_ms);
	if (rc <= 0)
		return -1;
	if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
		return -1;
	return (pfd.revents & POLLIN) ? 0 : -1;
}

static int wifi_platform_host_uart_read_byte(uint8_t *byte, uint32_t timeout_ms)
{
	ssize_t n;

	if (!byte)
		return -1;
	if (s_host_uart_fd < 0)
		return -1;
	if (host_uart_poll_in(timeout_ms) != 0)
		return -1;
	n = read(s_host_uart_fd, byte, 1);
	return (n == 1) ? 0 : -1;
}

static int wifi_platform_host_uart_write_byte(uint8_t byte)
{
	if (s_host_uart_fd < 0)
		return 0;
	return write(s_host_uart_fd, &byte, 1) == 1 ? 0 : -1;
}

static int wifi_platform_host_uart_read_bytes(uint8_t *buffer, size_t len,
					      size_t *out_len, uint32_t timeout_ms)
{
	size_t got = 0;

	if (out_len)
		*out_len = 0;
	if (!buffer || len == 0)
		return -1;
	if (s_host_uart_fd < 0)
		return -1;

	while (got < len) {
		ssize_t n;
		uint32_t slice = timeout_ms;

		if (got > 0u)
			slice = 20u;
		if (host_uart_poll_in(slice) != 0)
			break;
		n = read(s_host_uart_fd, buffer + got, len - got);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return -1;
		}
		if (n == 0)
			break;
		got += (size_t)n;
	}
	if (out_len)
		*out_len = got;
	return got > 0 ? 0 : -1;
}

static int wifi_platform_host_uart_write_bytes(const uint8_t *buffer, size_t len)
{
	size_t sent = 0;

	if (!buffer)
		return -1;
	if (len == 0)
		return 0;
	/* Unbound: keep historical stub success so unit tests that only TX pass. */
	if (s_host_uart_fd < 0)
		return 0;

	while (sent < len) {
		ssize_t n = write(s_host_uart_fd, buffer + sent, len - sent);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		sent += (size_t)n;
	}
	return 0;
}

static int wifi_platform_host_uart_flush(void)
{
	return 0;
}

static const wifi_platform_uart_ops_t wifi_platform_host_uart_ops = {
	.read_byte = wifi_platform_host_uart_read_byte,
	.write_byte = wifi_platform_host_uart_write_byte,
	.read_bytes = wifi_platform_host_uart_read_bytes,
	.write_bytes = wifi_platform_host_uart_write_bytes,
	.flush = wifi_platform_host_uart_flush,
};

const wifi_platform_uart_ops_t *wifi_platform_get_uart_ops(void)
{
	return &wifi_platform_host_uart_ops;
}

fl_result_t wifi_platform_get_ms(uint32_t *ms_out)
{
	int64_t ns = 0;

	if (!ms_out)
		return FL_RESULT_INVAL;
	if (fl_time_monotonic_ns(&ns) != FL_RESULT_OK)
		return FL_RESULT_ERR;
	*ms_out = (uint32_t)((ns > 0) ? (ns / 1000000) : 0);
	return FL_RESULT_OK;
}

void wifi_platform_sleep_ms(uint32_t ms)
{
	if (ms == 0u)
		return;
#if defined(__linux__) || defined(__APPLE__)
	if (ms >= 1000u) {
		unsigned long sec = (unsigned long)(ms / 1000u);
		unsigned long rem = (unsigned long)(ms % 1000u);
		sleep(sec);
		if (rem > 0u)
			(void)usleep((useconds_t)rem * 1000u);
		return;
	}
#endif
	(void)usleep((useconds_t)ms * 1000u);
}

void *wifi_platform_malloc(size_t size)
{
	return malloc(size);
}

void *wifi_platform_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

void wifi_platform_free(void *ptr)
{
	free(ptr);
}
