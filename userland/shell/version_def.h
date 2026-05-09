#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * Semantic version A.B.C — edit integers here; VERSION string is derived automatically.
 *
 *   A (VERSION_MAJOR)     — milestones / architecture-scale changes
 *   B (VERSION_STANDARD)  — new features (maps to semver "minor")
 *   C (VERSION_PATCH)     — bug fixes and small corrections (maps to semver "patch")
 *
 * Must match the highest A.B.C declared under version/entries/*.ver (CI checks).
 * Release changelog text is not compiled into normal local builds. CI runs
 * scripts/gen_version_changelog.c against version/entries and emits
 * userland/shell/version_changelog.c; see scripts/templates/version_changelog.example.c
 * and scripts/export_version_record.sh.
 */
#define VERSION_MAJOR    2
#define VERSION_STANDARD 2
#define VERSION_PATCH    5

#define VERSION_STR_(x) #x
#define VERSION_STR(x) VERSION_STR_(x)

/* Dotted release label "A.B.C" — keep using the VERSION macro in printf/help. */
#define VERSION \
    VERSION_STR(VERSION_MAJOR) "." VERSION_STR(VERSION_STANDARD) "." VERSION_STR(VERSION_PATCH)

#endif /* VERSION_DEF_H */
