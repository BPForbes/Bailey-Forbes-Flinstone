/**
 * **P1-2 — Address space story** (module contract, normative).
 *
 * Obligations:
 *   - Document **flat** vs **paged** memory models for **H** vs **K/B**; no silent
 *     conflation of host `malloc` arenas with kernel page tables on **B**.
 *   - On **H**, document **W^X** expectations where `mmap` is used (`PROT_*` policy).
 *   - On **K/B**, reference arch MMU programming (**ARMv8-A** / **Intel SDM**) in
 *     architecture docs tied to this contract.
 *
 * See **docs/ROADMAP.md** Phase 1; **Appendix D** for bare-metal MMU notes.
 */
#ifndef FL_CONTRACT_P1_ADDRESS_SPACE_H
#define FL_CONTRACT_P1_ADDRESS_SPACE_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_2_ADDRESS_SPACE_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P1_ADDRESS_SPACE_H */
