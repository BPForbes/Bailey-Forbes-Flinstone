/* **P9-1 — Fuzzing (hosted harnesses)** (module contract, normative).
 *
 * **Distribution:** **Fuzz inputs** (bytes, syscall sequences, netdev frames, FS metadata) are
 * **owned** by the harness until passed into the **SUT** (system under test). **Findings**
 * (crashes, hangs, sanitizer reports) are **exported** as **artifacts** with stable **severity**
 * tags for triage. **Corpus** directories may live in CI cache or repo **LFS**; paths obey caps
 * below.
 *
 * **Outcomes:** harness setup failures return **`fl_result_t`** (**P0-2**). A reproducing input
 * is **never silently dropped** once the engine accepts it.
 */
#ifndef FL_CONTRACT_P9_FUZZ_H
#define FL_CONTRACT_P9_FUZZ_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P9_1_FUZZ_CONTRACT_DEFINED 1

/** Inclusive max bytes for a single fuzz input blob handed to the SUT per iteration. */
#define FL_CONTRACT_P9_FUZZ_INPUT_MAX_BYTES (16u * 1024u * 1024u)

/** Inclusive max chars for a corpus directory or seed file path in lab metadata. */
#define FL_CONTRACT_P9_FUZZ_CORPUS_PATH_MAX_CHARS 4096u

/** Every crash under sanitizers is a defect until classified as tool noise (document waiver). */
#define FL_CONTRACT_P9_FUZZ_CRASH_IS_BUG_DEFAULT 1

_Static_assert(FL_CONTRACT_P9_FUZZ_CRASH_IS_BUG_DEFAULT == 1,
               "fuzz contract keeps crash triage explicit");

#endif /* FL_CONTRACT_P9_FUZZ_H */
