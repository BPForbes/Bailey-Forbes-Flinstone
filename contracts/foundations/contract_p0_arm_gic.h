/**
 * **P0-4 — ARM GIC EOI correctness** (module contract, normative).
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

#define FL_CONTRACT_P0_4_GIC_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P0_ARM_GIC_H */
