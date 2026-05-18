#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * GENERATED FILE — do not edit VERSION_* or VERSION_LINE by hand.
 *
 * Shipped numeric semver (VERSION_MAJOR/STANDARD/PATCH and VERSION): normally the highest
 * A.B.C among all .ver files under version/locked/ (recursive); PRERELEASE, GM, and
 * DEV_VERSION there are ignored for those macros.
 *
 * Go-to-main (entries): when any .ver under version/entries/ has PRERELEASE=1 and GM=1,
 * the newest such row (highest semver, then highest DEV_VERSION) supplies MAJOR/STANDARD/
 * RELEASE for VERSION_* / VERSION (RELEASE maps to VERSION_PATCH). VERSION_LINE is then
 * plain A.B.C with no prerelease tag and no ", BUILD n" suffix (for example "4.0.0").
 * GitHub Actions run this script and commit the refreshed header: .github/workflows/c-cpp.yml
 * (same-repo PR / feature-branch bot push after relocate), .github/workflows/version-lock-on-merge.yml
 * after pushes to develop (sync PR), and .github/actions/sync-version-checkout (build jobs).
 *
 * Otherwise VERSION_LINE follows version/entries PRERELEASE=1 rows: PRERELEASE_TAG
 * (default PRE), semver A.B.C, and optional ", BUILD n" when DEV_VERSION>=1; if no
 * PRERELEASE=1 rows exist, VERSION_LINE matches VERSION.
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

#define VERSION_LINE "PRE 4.0.0, BUILD 14"

#endif /* VERSION_DEF_H */
