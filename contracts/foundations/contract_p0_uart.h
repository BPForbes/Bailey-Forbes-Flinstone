/**
 * **P0-8 — Early serial console (UART)** (module contract, normative).
 *
 * **Interchange blueprint** (**FL_CONTRACT_P0_UART_INTERCHANGE_REV**): frozen early
 * serial output surface. Bump only with a **version/entries** note when semantics change.
 *
 * Named surface(s):
 *   - **uart_early_putc** — single character → hardware FIFO / MMIO register
 *
 * Ownership / lifetime:
 *   - Caller owns the **character**; UART hardware is **static** for bring-up; no
 *     buffer ownership crosses the boundary (contrast **P6** structured logger).
 *
 * Error / status channel:
 *   - FIFO full or absent UART: **busy-wait** or **drop** per documented early policy
 *     (not **fl_result_t** on the hot path); mis-config is bring-up checklist failure.
 *   - Targets without early UART may record **FL_CONTRACT_P0_UART_WAIVER_ACCEPTED**
 *     with **version/entries** note.
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

#define FL_CONTRACT_P0_UART_INTERCHANGE_REV 1

#define FL_CONTRACT_P0_UART_SURFACE_EARLY_PUTC 0
#define FL_CONTRACT_P0_UART_SURFACE_COUNT 1

typedef enum {
    fl_contract_p0_uart_surface_early_putc = FL_CONTRACT_P0_UART_SURFACE_EARLY_PUTC,
} fl_contract_p0_uart_surface_t;

#define FL_CONTRACT_P0_UART_LIFETIME_REGISTER_STATIC 0

#define FL_CONTRACT_P0_UART_STATUS_BUSY_WAIT_OR_DROP 1

#ifndef FL_CONTRACT_P0_UART_WAIVER_ACCEPTED
#define FL_CONTRACT_P0_UART_WAIVER_ACCEPTED 0
#endif

#define FL_CONTRACT_P0_8_UART_CONTRACT_DEFINED 1

_Static_assert(FL_CONTRACT_P0_UART_SURFACE_COUNT == 1,
               "fl_contract_p0_uart_surface_t: expected one named surface");

#endif /* FL_CONTRACT_P0_UART_H */
