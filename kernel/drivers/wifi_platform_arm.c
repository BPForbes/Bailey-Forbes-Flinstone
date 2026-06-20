/*
 * WiFi Platform Implementation (ARM UART)
 * Integrates with existing ARM PL011 UART driver for WiFi coprocessor I/O
 */

#include <string.h>
#include <stdlib.h>

#include "kernel/drivers/wifi_platform.h"
#include "kernel/core/time/timekeeping.h"

#include "fl/mm.h"

/* External ARM UART functions (from kernel/arch/aarch64/hal/arm_uart.c) */
extern int arm_uart_poll(uint8_t *out);
extern int arm_uart_getchar(char *out);
extern void arm_uart_putchar(char c);

/* Platform-specific UART operations implementation */

static int wifi_platform_uart_read_byte(uint8_t *byte, uint32_t timeout_ms)
{
	if (!byte) {
		return -1;
	}

	/* ARM UART is non-blocking poll-based; timeout handled by caller */
	return arm_uart_poll(byte);
}

static int wifi_platform_uart_write_byte(uint8_t byte)
{
	arm_uart_putchar((char)byte);
	return 0;
}

static int wifi_platform_uart_read_bytes(uint8_t *buffer, size_t len, size_t *out_len,
					 uint32_t timeout_ms)
{
	if (!buffer || len == 0 || !out_len) {
		return -1;
	}

	*out_len = 0;

	uint32_t start_ms = 0;
	size_t bytes_read = 0;

	if (wifi_platform_get_ms(&start_ms) != FL_RESULT_OK)
		return -1;

	while (bytes_read < len) {
		uint8_t byte;
		uint32_t now_ms = 0;

		if (arm_uart_poll(&byte) == 0) {
			buffer[bytes_read++] = byte;
			if (wifi_platform_get_ms(&start_ms) != FL_RESULT_OK)
				return -1;
		} else {
			if (wifi_platform_get_ms(&now_ms) != FL_RESULT_OK)
				return -1;
			if (now_ms - start_ms > timeout_ms)
				break;
			wifi_platform_sleep_ms(1);
		}
	}

	*out_len = bytes_read;
	return bytes_read > 0 ? 0 : -1;
}

static int wifi_platform_uart_write_bytes(const uint8_t *buffer, size_t len)
{
	if (!buffer || len == 0) {
		return -1;
	}

	for (size_t i = 0; i < len; i++) {
		arm_uart_putchar((char)buffer[i]);
	}

	return 0;
}

static int wifi_platform_uart_flush(void)
{
	/* ARM PL011 auto-flushes; no-op */
	return 0;
}

static const wifi_platform_uart_ops_t wifi_platform_uart_ops = {
	.read_byte = wifi_platform_uart_read_byte,
	.write_byte = wifi_platform_uart_write_byte,
	.read_bytes = wifi_platform_uart_read_bytes,
	.write_bytes = wifi_platform_uart_write_bytes,
	.flush = wifi_platform_uart_flush,
};

const wifi_platform_uart_ops_t *wifi_platform_get_uart_ops(void)
{
	return &wifi_platform_uart_ops;
}

/* Platform time utilities */
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
	int64_t start_ns = 0;
	int64_t deadline_ns;

	if (ms == 0u)
		return;
	if (fl_time_monotonic_ns(&start_ns) != FL_RESULT_OK)
		return;
	deadline_ns = start_ns + (int64_t)ms * 1000000LL;
	for (;;) {
		int64_t now_ns = 0;

		if (fl_time_monotonic_ns(&now_ns) != FL_RESULT_OK)
			return;
		if (now_ns >= deadline_ns)
			break;
	}
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
