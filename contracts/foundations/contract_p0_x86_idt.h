/**
 * **P0-5 — x86_64 IDT + IRQ0 timer tick** (module contract, normative).
 *
 * **Interchange blueprint** (**FL_CONTRACT_P0_IDT_INTERCHANGE_REV**): frozen IDT/IRQ0
 * tick surface. Bump only with a **version/entries** note when semantics change.
 *
 * Named surface(s):
 *   - **idt_irq0_tick** — vector dispatch → canonical tick counter increment
 *
 * Ownership / lifetime:
 *   - **IDT** table and handler pointers are **static** for the boot session;
 *     tick counter storage is **kernel-owned** with handler **transient** re-entry.
 *
 * Error / status channel:
 *   - Mis-vector or missing handler faults via **CPU exception** path (not
 *     **fl_result_t**); bring-up gates use observable tick deltas in **B** builds.
 *
 * Obligations:
 *   - Install a minimal **IDT** with a documented vector for **IRQ0** (e.g. **0x20**
 *     per project convention) such that the tick path can advance **`hw_tick_count()`**
 *     (or the project’s canonical tick counter) between observable calls in **B** builds.
 *   - Ordering relative to **P0-6** (**GDT**) must follow **Intel SDM** and **Appendix D**
 *     (GDT before relying on IDT/IRQ where applicable).
 *
 * Implementation: kernel/arch/x86_64/boot/ (IDT, PIC/APIC wiring); this file
 * names the contract for phase gates (P0 to P1 transition).
 */
#ifndef FL_CONTRACT_P0_X86_IDT_H
#define FL_CONTRACT_P0_X86_IDT_H

#define FL_CONTRACT_P0_IDT_INTERCHANGE_REV 1

#define FL_CONTRACT_P0_IDT_SURFACE_IRQ0_TICK 0
#define FL_CONTRACT_P0_IDT_SURFACE_COUNT 1

typedef enum {
    fl_contract_p0_idt_surface_irq0_tick = FL_CONTRACT_P0_IDT_SURFACE_IRQ0_TICK,
} fl_contract_p0_idt_surface_t;

#define FL_CONTRACT_P0_IDT_LIFETIME_STATIC_TABLE 0
#define FL_CONTRACT_P0_IDT_LIFETIME_TICK_COUNTER_KERNEL 1

#define FL_CONTRACT_P0_IDT_STATUS_CPU_EXCEPTION 1

#define FL_CONTRACT_P0_5_IDT_IRQ0_CONTRACT_DEFINED 1

_Static_assert(FL_CONTRACT_P0_IDT_SURFACE_COUNT == 1,
               "fl_contract_p0_idt_surface_t: expected one named surface");

#endif /* FL_CONTRACT_P0_X86_IDT_H */
