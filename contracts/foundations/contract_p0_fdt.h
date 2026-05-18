/**
 * **P0-7 — Device tree (FDT / DTB) metadata** (module contract, normative).
 *
 * **Interchange blueprint** (**FL_CONTRACT_P0_FDT_INTERCHANGE_REV**): frozen DTB
 * handoff surface. Bump only with a **version/entries** note when semantics change.
 *
 * Named surface(s):
 *   - **fdt_dtb_handoff** — firmware DTB pointer → parsed memory / **cpus** / **compatible**
 *
 * Ownership / lifetime:
 *   - DTB blob is **read-only** for the boot session (**immutable** mapping); parsed
 *     nodes are **transient** walker views (no ownership transfer to drivers until
 *     **P4-6** attach policy runs).
 *
 * Error / status channel:
 *   - Malformed DTB: walker returns **FL_RESULT_INVAL** (or **FL_RESULT_ERR** when
 *     unspecified); missing DTB on targets that require it is a **documented waiver**
 *     (**FL_CONTRACT_P0_FDT_WAIVER_ACCEPTED**) with **version/entries** note.
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

#define FL_CONTRACT_P0_FDT_INTERCHANGE_REV 1

#define FL_CONTRACT_P0_FDT_SURFACE_DTB_HANDOFF 0
#define FL_CONTRACT_P0_FDT_SURFACE_COUNT 1

typedef enum {
    fl_contract_p0_fdt_surface_dtb_handoff = FL_CONTRACT_P0_FDT_SURFACE_DTB_HANDOFF,
} fl_contract_p0_fdt_surface_t;

#define FL_CONTRACT_P0_FDT_LIFETIME_DTB_IMMUTABLE 0
#define FL_CONTRACT_P0_FDT_LIFETIME_NODE_VIEW_TRANSIENT 1

#define FL_CONTRACT_P0_FDT_STATUS_FL_RESULT 0

/**
 * Set to **1** only with a **version/entries** note when DTB handoff is intentionally
 * waived for a target (visible to **grep** / reviews).
 */
#ifndef FL_CONTRACT_P0_FDT_WAIVER_ACCEPTED
#define FL_CONTRACT_P0_FDT_WAIVER_ACCEPTED 0
#endif

#define FL_CONTRACT_P0_7_FDT_CONTRACT_DEFINED 1

_Static_assert(FL_CONTRACT_P0_FDT_SURFACE_COUNT == 1,
               "fl_contract_p0_fdt_surface_t: expected one named surface");

#endif /* FL_CONTRACT_P0_FDT_H */
