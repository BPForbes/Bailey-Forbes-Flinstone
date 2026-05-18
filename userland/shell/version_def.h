#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * GENERATED FILE — do not edit by hand.
 *
 * Shipped numeric semver (VERSION_MAJOR/STANDARD/PATCH and VERSION) comes from the
 * highest A.B.C among all .ver files under version/locked/ (recursive). PRERELEASE,
 * GM, and DEV_VERSION there are ignored for those macros (see docs/versioning.md).
 *
 * VERSION_LINE is the shell-facing display string: when any .ver under version/entries/
 * has PRERELEASE=1, it shows PRERELEASE_TAG (default PRE), that row semver A.B.C, and
 * optional ", BUILD n" (n from DEV_VERSION) when DEV_VERSION>=1; otherwise VERSION_LINE
 * matches VERSION.
 *
 * To bump shipped semver: add new .ver files under version/entries/, finalize to
 * version/locked/, then run make or ./scripts/gen_version_def.sh.
 */
#define VERSION_MAJOR    3
#define VERSION_STANDARD 3
#define VERSION_PATCH    0

#define VERSION_STR_(x) #x
#define VERSION_STR(x) VERSION_STR_(x)

#define VERSION \
    VERSION_STR(VERSION_MAJOR) "." VERSION_STR(VERSION_STANDARD) "." VERSION_STR(VERSION_PATCH)

#define VERSION_LINE "PRE 4.0.0, BUILD 12"

#endif /* VERSION_DEF_H */
