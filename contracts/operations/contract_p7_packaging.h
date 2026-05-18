/* P7-2 Packaging (module contract, normative). Shipped **VERSION** and public API semver
 * follow **version/locked/** (not hand-edited headers on feature branches). SBOM manifests
 * and reproducible tarball/OSTree flows are optional hosted deliverables. See
 * docs/ROADMAP.md Phase 7. */
#ifndef FL_CONTRACT_P7_PACKAGING_H
#define FL_CONTRACT_P7_PACKAGING_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P7_2_PACKAGING_CONTRACT_DEFINED 1

/** Packaging must read headline **A.B.C** from **version/locked/**/*.ver** pipeline. */
#define FL_CONTRACT_P7_PACKAGING_VERSION_FROM_LOCKED 1

/** SBOM attachment is optional for lab tarballs; when present, path cap applies. */
#define FL_CONTRACT_P7_SBOM_MANIFEST_OPTIONAL 1

#define FL_CONTRACT_P7_SBOM_PATH_MAX_CHARS 4096u

typedef enum {
    FL_CONTRACT_P7_ARTIFACT_SIGN_NONE = 0,
    FL_CONTRACT_P7_ARTIFACT_SIGN_MINISIGN_DEFERRED = 1,
    FL_CONTRACT_P7_ARTIFACT_SIGN_SIGNIFY_DEFERRED = 2,
    FL_CONTRACT_P7_ARTIFACT_SIGN_PKCS7_DEFERRED = 3
} fl_contract_p7_artifact_sign_method_t;

_Static_assert(FL_CONTRACT_P7_PACKAGING_VERSION_FROM_LOCKED == 1,
               "packaging version source must remain locked-tree driven");

#endif /* FL_CONTRACT_P7_PACKAGING_H */
