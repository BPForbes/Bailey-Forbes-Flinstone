/**
 * **P0-5 — x86_64 IDT + IRQ0 timer tick** (module contract, normative).
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

#define FL_CONTRACT_P0_5_IDT_IRQ0_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P0_X86_IDT_H */
