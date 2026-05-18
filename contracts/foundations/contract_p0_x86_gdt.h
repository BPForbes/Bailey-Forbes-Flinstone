/**
 * **P0-6 — x86_64 GDT (minimal flat)** (module contract, normative).
 *
 * **Interchange blueprint** (**FL_CONTRACT_P0_GDT_INTERCHANGE_REV**): frozen GDT
 * install surface. Bump only with a **version/entries** note when semantics change.
 *
 * Named surface(s):
 *   - **gdt_install** — descriptor table image → **lgdt** + segment reload sequence
 *
 * Ownership / lifetime:
 *   - GDT image is **static** for the boot session; reload is **one-shot** before
 *     protected-mode IDT/IRQ paths that assume valid segment state (**P0-5**).
 *
 * Error / status channel:
 *   - Invalid descriptor state surfaces as **CPU exception** on first faulting
 *     access; bring-up verification is checklist-driven (**Appendix D**).
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

#define FL_CONTRACT_P0_GDT_INTERCHANGE_REV 1

#define FL_CONTRACT_P0_GDT_SURFACE_INSTALL 0
#define FL_CONTRACT_P0_GDT_SURFACE_COUNT 1

typedef enum {
    fl_contract_p0_gdt_surface_install = FL_CONTRACT_P0_GDT_SURFACE_INSTALL,
} fl_contract_p0_gdt_surface_t;

#define FL_CONTRACT_P0_GDT_LIFETIME_STATIC_IMAGE 0

#define FL_CONTRACT_P0_GDT_STATUS_CPU_EXCEPTION 1

#define FL_CONTRACT_P0_6_GDT_CONTRACT_DEFINED 1

_Static_assert(FL_CONTRACT_P0_GDT_SURFACE_COUNT == 1,
               "fl_contract_p0_gdt_surface_t: expected one named surface");

#endif /* FL_CONTRACT_P0_X86_GDT_H */
