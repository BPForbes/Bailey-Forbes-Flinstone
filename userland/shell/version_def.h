#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * GENERATED FILE — do not edit by hand.
 *
 * Built from finalized .ver files under version/locked/ by scripts/gen_version_def.sh (also run from the Makefile).
 * The shipped version is the highest A.B.C among those files (additional release note files may exist only under version/entries/ until finalize).
 *
 *   A (VERSION_MAJOR)     — milestones / architecture-scale changes
 *   B (VERSION_STANDARD)  — new features (semver "minor")
 *   C (VERSION_PATCH)     — fixes and small corrections (semver "patch")
 *
 * To bump: add version/entries/<A>_<B>_<C>_<slug>.ver, run ./scripts/finalize_version_locked.sh, then `make` or `./scripts/gen_version_def.sh`.
 */
#define VERSION_MAJOR    2
#define VERSION_STANDARD 3
#define VERSION_PATCH    0

#define VERSION_STR_(x) #x
#define VERSION_STR(x) VERSION_STR_(x)

#define VERSION \
    VERSION_STR(VERSION_MAJOR) "." VERSION_STR(VERSION_STANDARD) "." VERSION_STR(VERSION_PATCH)

#endif /* VERSION_DEF_H */
