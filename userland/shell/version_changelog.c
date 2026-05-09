#include "version_def.h"

/*
 * Running tab of release notes (short lines; extend downward each release).
 * Deployment scripts can concatenate records from multiple branches — see scripts/export_version_record.sh.
 */
const char VERSION_CHANGELOG[256] =
    "2.2.4 — Structured VERSION_MAJOR/STANDARD/PATCH + changelog export script.\n";
