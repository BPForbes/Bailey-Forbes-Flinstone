#ifndef VERSION_DEF_H
#define VERSION_DEF_H

/*
 * Semantic version A.B.C — edit integers here; VERSION string is derived automatically.
 *
 *   A (VERSION_MAJOR)     — milestones / architecture-scale changes
 *   B (VERSION_STANDARD)  — new features (maps to semver "minor")
 *   C (VERSION_PATCH)     — bug fixes and small corrections (maps to semver "patch")
 *
 * Running narrative lives in version_changelog.c as VERSION_CHANGELOG[].
 */
#define VERSION_MAJOR    2
#define VERSION_STANDARD 2
#define VERSION_PATCH    4

#define VERSION_STR_(x) #x
#define VERSION_STR(x) VERSION_STR_(x)

/* Dotted release label "A.B.C" — keep using the VERSION macro in printf/help. */
#define VERSION \
    VERSION_STR(VERSION_MAJOR) "." VERSION_STR(VERSION_STANDARD) "." VERSION_STR(VERSION_PATCH)

extern const char VERSION_CHANGELOG[256];

#endif /* VERSION_DEF_H */
