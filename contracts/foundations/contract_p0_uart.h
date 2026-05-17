/**
 * **P0-8 — Early serial console (UART)** (module contract, normative).
 *
 * Obligations:
 *   - Provide **bring-up output** on a documented UART (**NS16550**-compatible PC/QEMU
 *     **isa-serial**, or **ARM PL011** on common AArch64 models) before full display
 *     paths are available, when early console is in scope for the target.
 *   - Document **early log** policy vs the structured **P6** logger (rate limits,
 *     IRQ safety, and whether **early_putc** may run with interrupts masked).
 *
 * Register-level code stays in arch drivers; this header is the **contract anchor**
 * for ordering relative to P0-5 and P0-4 bring-up.
 */
#ifndef FL_CONTRACT_P0_UART_H
#define FL_CONTRACT_P0_UART_H

#define FL_CONTRACT_P0_8_UART_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P0_UART_H */
