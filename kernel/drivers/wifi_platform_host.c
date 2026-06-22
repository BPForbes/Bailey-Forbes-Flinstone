/*
 * WiFi Platform Implementation (hosted x86/x64)
 * Stub UART ops for CI and desktop builds without ARM PL011 hardware.
 */

#include "kernel/drivers/wifi_platform.h"
#include "kernel/core/time/timekeeping.h"

#include <stdlib.h>
#include <unistd.h>

static int wifi_platform_host_uart_read_byte(uint8_t *byte, uint32_t timeout_ms)
{
	(void)byte;
	(void)timeout_ms;
	return -1;
}

static int wifi_platform_host_uart_write_byte(uint8_t byte)
{
	(void)byte;
	return 0;
}

static int wifi_platform_host_uart_read_bytes(uint8_t *buffer, size_t len,
					      size_t *out_len, uint32_t timeout_ms)
{
	(void)buffer;
	(void)len;
	(void)timeout_ms;
	if (out_len)
		*out_len = 0;
	return -1;
}

static int wifi_platform_host_uart_write_bytes(const uint8_t *buffer, size_t len)
{
	(void)buffer;
	return len > 0 ? 0 : -1;
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
