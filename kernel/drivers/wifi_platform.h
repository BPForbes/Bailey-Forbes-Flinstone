/*
 * WiFi Platform Abstraction
 * Provides platform-specific UART and bus access for WiFi drivers
 */

#ifndef KERNEL_DRIVERS_WIFI_PLATFORM_H
#define KERNEL_DRIVERS_WIFI_PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "contract_result.h"

/* Platform UART interface */
typedef struct {
	int (*read_byte)(uint8_t *byte, uint32_t timeout_ms);
	int (*write_byte)(uint8_t byte);
	int (*read_bytes)(uint8_t *buffer, size_t len, size_t *out_len,
			 uint32_t timeout_ms);
	int (*write_bytes)(const uint8_t *buffer, size_t len);
	int (*flush)(void);
} wifi_platform_uart_ops_t;

/* Get platform UART ops for WiFi drivers */
const wifi_platform_uart_ops_t *wifi_platform_get_uart_ops(void);

/* Platform time utilities (fail closed when monotonic time is unavailable). */
fl_result_t wifi_platform_get_ms(uint32_t *ms_out);
void wifi_platform_sleep_ms(uint32_t ms);

/* Platform memory allocation */
void *wifi_platform_malloc(size_t size);
void *wifi_platform_realloc(void *ptr, size_t size);
void wifi_platform_free(void *ptr);

#endif /* KERNEL_DRIVERS_WIFI_PLATFORM_H */
