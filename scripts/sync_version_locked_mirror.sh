#!/usr/bin/env bash
# Deprecated script name — calls finalize_version_locked.sh (entries → locked).
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/finalize_version_locked.sh"
