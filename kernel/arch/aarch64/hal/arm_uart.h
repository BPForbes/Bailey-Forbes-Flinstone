/**
 * ARM PL011 UART - console input for keyboard driver.
 */
#ifndef ARM_UART_H
#define ARM_UART_H

#include <stdint.h>

int arm_uart_poll(uint8_t *out);
int arm_uart_getchar(char *out);
void arm_uart_putchar(char c);

#endif /* ARM_UART_H */
