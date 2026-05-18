/**
 * **P0-4 — ARM GIC EOI correctness** (module contract, normative).
 *
 * **Interchange blueprint** (**FL_CONTRACT_P0_GIC_INTERCHANGE_REV**): frozen
 * EOI write-path model. Bump only with a **version/entries** note when surface,
 * ownership, lifetime, or error semantics change.
 *
 * Named surface(s):
 *   - **gic_eoi_path** — active IRQ id → priority-drop + deactivate write
 *
 * Ownership / lifetime:
 *   - **IRQ id** is caller-owned for the duration of the handler; the GIC register
 *     write is **fire-and-forget** (no buffer lifetime beyond the store).
 *
 * Error / status channel:
 *   - Wrong-IRQ **EOI** is a **silent priority inversion** (no **fl_result_t**);
 *     detection is by trace / checklist (**Appendix D**), not errno.
 *
 * Obligations:
 *   - **EOI** / priority-drop paths must acknowledge the **same IRQ** that was
 *     taken from the distributor; no hard-coded sentinel IRQ number that does not
 *     match the active exception.
 *   - Behaviour must be traceable to **ARM GIC** architecture documentation; fix
 *     patterns belong in **Appendix D** (bare-metal checklist).
 *
 * Implementation lives under kernel/arch/aarch64/ and related IRQ paths;
 * this header is the contract anchor for reviews and B/K bring-up gates.
 */
#ifndef FL_CONTRACT_P0_ARM_GIC_H
#define FL_CONTRACT_P0_ARM_GIC_H

#define FL_CONTRACT_P0_GIC_INTERCHANGE_REV 1

#define FL_CONTRACT_P0_GIC_SURFACE_EOI_PATH 0
#define FL_CONTRACT_P0_GIC_SURFACE_COUNT 1

typedef enum {
    fl_contract_p0_gic_surface_eoi_path = FL_CONTRACT_P0_GIC_SURFACE_EOI_PATH,
} fl_contract_p0_gic_surface_t;

#define FL_CONTRACT_P0_GIC_LIFETIME_REGISTER_WRITE 0

#define FL_CONTRACT_P0_GIC_STATUS_PRIORITY_INVERSION 1

#define FL_CONTRACT_P0_4_GIC_CONTRACT_DEFINED 1

_Static_assert(FL_CONTRACT_P0_GIC_SURFACE_COUNT == 1,
               "fl_contract_p0_gic_surface_t: expected one named surface");

#endif /* FL_CONTRACT_P0_ARM_GIC_H */
