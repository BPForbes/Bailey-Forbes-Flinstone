/*
 * WiFi Platform Implementation (hosted x86/x64)
 * Stub UART ops for CI and desktop builds without ARM PL011 hardware.
 */

#include <stdlib.h>

#include "kernel/drivers/wifi_platform.h"
#include "kernel/core/time/timekeeping.h"

#include "fl/mm.h"

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

uint32_t wifi_platform_get_ms(void)
{
	int64_t ns = 0;

	if (fl_time_monotonic_ns(&ns) != FL_RESULT_OK)
		return 0;
	return (uint32_t)((ns > 0) ? (ns / 1000000) : 0);
}

void wifi_platform_sleep_ms(uint32_t ms)
{
	(void)ms;
}

void *wifi_platform_malloc(size_t size)
{
	return kmalloc(size);
}

void *wifi_platform_realloc(void *ptr, size_t size)
{
	return krealloc(ptr, size);
}

void wifi_platform_free(void *ptr)
{
	kfree(ptr);
}
