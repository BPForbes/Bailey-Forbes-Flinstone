#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * GENERATED FILE — do not edit by hand.
 *
 * Built from version entry files (.ver) under version/entries/ by scripts/gen_version_def.sh (also run from the Makefile).
 * The shipped version is the highest A.B.C among those entry files.
 *
 *   A (VERSION_MAJOR)     — milestones / architecture-scale changes
 *   B (VERSION_STANDARD)  — new features (semver "minor")
 *   C (VERSION_PATCH)     — fixes and small corrections (semver "patch")
 *
 * To bump: add a new version/entries/<A>_<B>_<C>_<slug>.ver file (see version/entries/ABOUT.txt),
 * then run `make` or `./scripts/gen_version_def.sh` and commit the regenerated header.
 */
#define VERSION_MAJOR    2
#define VERSION_STANDARD 2
#define VERSION_PATCH    4

#define VERSION_STR_(x) #x
#define VERSION_STR(x) VERSION_STR_(x)

#define VERSION \
    VERSION_STR(VERSION_MAJOR) "." VERSION_STR(VERSION_STANDARD) "." VERSION_STR(VERSION_PATCH)

#endif /* VERSION_DEF_H */
