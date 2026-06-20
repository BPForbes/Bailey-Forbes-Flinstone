/*
 * WiFi Platform Implementation (ARM UART)
 * Integrates with existing ARM PL011 UART driver for WiFi coprocessor I/O
 */

#include <string.h>
#include <stdlib.h>

#include "kernel/drivers/wifi_platform.h"

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

	/* Simple polling approach; real implementation would use interrupts */
	uint32_t start_ms = wifi_platform_get_ms();
	size_t bytes_read = 0;

	while (bytes_read < len) {
		uint8_t byte;

		if (arm_uart_poll(&byte) == 0) {
			buffer[bytes_read++] = byte;
			start_ms = wifi_platform_get_ms(); /* Reset timeout on successful read */
		} else {
			/* Check timeout */
			if (wifi_platform_get_ms() - start_ms > timeout_ms) {
				break;
			}
			wifi_platform_sleep_ms(1); /* Yield CPU */
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
uint32_t wifi_platform_get_ms(void)
{
	/* TODO: Integrate with kernel clock/scheduler
	 * For now, return a placeholder that incrementally increases
	 * Real implementation should use kernel timer
	 */
	static uint32_t ticks = 0;
	return ticks++;
}

void wifi_platform_sleep_ms(uint32_t ms)
{
	/* TODO: Use kernel scheduler yield
	 * For now, busy-wait (not ideal but functional)
	 */
	(void)ms;
	/* Placeholder */
}

/* Platform memory allocation (use kernel allocator) */
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
