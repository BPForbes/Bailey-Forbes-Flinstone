/**
 * **P0-6 — x86_64 GDT (minimal flat)** (module contract, normative).
 *
 * Obligations:
 *   - A **known-good minimal flat GDT** is installed (**lgdt**, segment reload as
 *     required) **before** code relies on protected-mode IDT/IRQ paths that assume
 *     valid segment state.
 *   - Document the reload sequence in docs/ or the arch README; reference
 *     **Appendix D** execution rows that apply.
 *
 * Implementation: **kernel/arch/x86_64/boot/gdt** (and related); this header is
 * the **contract anchor** for **P0-5** dependencies.
 */
#ifndef FL_CONTRACT_P0_X86_GDT_H
#define FL_CONTRACT_P0_X86_GDT_H

#define FL_CONTRACT_P0_6_GDT_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P0_X86_GDT_H */
