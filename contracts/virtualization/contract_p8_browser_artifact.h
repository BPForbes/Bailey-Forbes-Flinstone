/**
 * **P8-3 — browser-kernel artifact interchange** (module contract, normative).
 *
 * This shard freezes the JSON manifest consumed across the build, CI promotion,
 * static hosting, and browser-lab boundary.  The producer owns both files until
 * publication completes; consumers treat the published artifact and manifest as
 * immutable and validate the declared SHA-256 before booting.
 *
 * **Required `build-info.json` fields and JSON types**
 *   - `schemaVersion` (integer, 1 or 2; producers emit 2)
 *   - `project`, `repository`, `commit`, `shortCommit`, `builtAt`,
 *     `architecture`, `cpuMode`, `artifact`, `artifactFormat`, `sha256`,
 *     `browserEmulator`, `bootSuccessMarker`, `validationOutcome` (strings)
 *   - `v86Compatible`, `bootable`, `bootSuccessMarkerImplemented` (booleans)
 *   - schema 2: `browserCompatible`, `bootableCandidate` (booleans),
 *     `capabilities` (object of subsystem-name to boolean availability), and,
 *     when browser-compatible, `browserValidation` containing: `commit` and
 *     `artifactSha256` matching the immutable guest; non-empty `runtime`,
 *     40-character `runtimeCommit`, and non-empty `runtimeFiles` basename-to-
 *     SHA-256 map matching the pinned emulator; parseable `testedAt`, non-empty
 *     `browser`, `serial` containing the exact boot-marker line, and `checks`
 *     containing `exact-marker`
 *   - `bootloader`, `requiredBios`, `qemuBootMode` (string or null)
 *   - `minimumRamBytes` (non-negative integer or null)
 *   - `recommendedRamBytes` (positive integer)
 *   - `blockers` (array of strings)
 *
 * `artifact` is a basename relative to the manifest. `sha256` is 64 lower-case
 * hexadecimal characters. `qemuBootMode`, when non-null, is `kernel` for QEMU
 * `-kernel` loading or `ide-drive` for a raw IDE disk image.
 *
 * **Fail-closed promotion:** metadata is necessary but not sufficient. CI may
 * publish `flintstone-browser-kernel` only when `bootable`, `browserCompatible`, and
 * `bootSuccessMarkerImplemented` are true **and** independent native-QEMU and
 * browser-QEMU-Wasm serial smoke tests both observe `FLINTSTONE_KERNEL_BOOT_OK`.
 * A manifest cannot self-attest either observation.
 */
#ifndef FL_CONTRACT_P8_BROWSER_ARTIFACT_H
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_H

#include "contract_extend.h"

#define FL_CONTRACT_P8_BROWSER_ARTIFACT_INTERCHANGE_REV 3
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_SCHEMA_VERSION 2
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_CONTRACT_DEFINED 1

#define FL_CONTRACT_P8_BROWSER_ARTIFACT_BOOT_MARKER "FLINTSTONE_KERNEL_BOOT_OK"
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_SHA256_HEX_CHARS 64u

#define FL_CONTRACT_P8_BROWSER_ARTIFACT_QEMU_BOOT_KERNEL "kernel"
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_QEMU_BOOT_IDE_DRIVE "ide-drive"

#define FL_CONTRACT_P8_BROWSER_ARTIFACT_OUTCOME_BOOTABLE "browser-bootable"
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_OUTCOME_BLOCKED "architecture-blocked"

#define FL_CONTRACT_P8_BROWSER_ARTIFACT_PROMOTION_REQUIRES_BOOTABLE 1
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_PROMOTION_REQUIRES_BROWSER_COMPATIBILITY 1
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_PROMOTION_REQUIRES_MARKER_DECLARATION 1
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_PROMOTION_REQUIRES_QEMU_OBSERVATION 1
#define FL_CONTRACT_P8_BROWSER_ARTIFACT_PROMOTION_REQUIRES_BROWSER_OBSERVATION 1

_Static_assert(FL_CONTRACT_SURFACE_BROWSER_ARTIFACT == 5,
               "browser artifact surface must remain append-only in the P0 ABI");
_Static_assert(FL_CONTRACT_P8_BROWSER_ARTIFACT_SHA256_HEX_CHARS == 64u,
               "SHA-256 manifest digest must contain 64 hexadecimal characters");

#endif /* FL_CONTRACT_P8_BROWSER_ARTIFACT_H */
