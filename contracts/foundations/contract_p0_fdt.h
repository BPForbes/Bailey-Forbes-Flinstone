/**
 * **P0-7 — Device tree (FDT / DTB) metadata** (module contract, normative).
 *
 * Obligations (for **B** / **K** **AArch64** beyond fixed QEMU-only tables):
 *   - Consume a firmware-passed DTB pointer (for example QEMU -dtb) for memory,
 *     **cpus**, and **compatible** strings used for driver match—avoid “one board only”
 *     hard-coded tables when a DTB is available.
 *   - **dtc** / spec references: Devicetree.org **FDT** specification; optional CI hooks
 *     tie back to **P0-3** (**contract_p0_ci.h**).
 *
 * Parser and walker implementation may live in loader vs kernel per project split;
 * this header **defines** the interchange expectations for **P4-6** consumers.
 */
#ifndef FL_CONTRACT_P0_FDT_H
#define FL_CONTRACT_P0_FDT_H

#define FL_CONTRACT_P0_7_FDT_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P0_FDT_H */
