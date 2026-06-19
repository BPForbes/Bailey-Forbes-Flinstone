#!/usr/bin/env bash
# Cloud-agent VM bootstrap: install build deps and verify default `make`.
# Invoked by Cursor cloud-agent install-user.sh (run with sudo).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_USER="${SUDO_USER:-}"

mkdir -p /var/lib/apt/lists/partial

# Full toolchain + SQLite/OpenSSL/SDL2/networking (see AGENTS.md, install_deps.sh).
bash "$REPO_ROOT/scripts/install_deps.sh"

# Fail fast with actionable errors instead of a long compile log.
test -f /usr/include/sqlite3.h || {
    echo "cloud_agent_install: FATAL — libsqlite3-dev missing (/usr/include/sqlite3.h)" >&2
    exit 1
}
command -v g++ >/dev/null || {
    echo "cloud_agent_install: FATAL — g++ missing" >&2
    exit 1
}
test -f /usr/include/openssl/ssl.h || {
    echo "cloud_agent_install: FATAL — libssl-dev missing (/usr/include/openssl/ssl.h)" >&2
    exit 1
}

# apt/install_deps run as root; verification build must not leave root-owned artifacts
# in a checkout owned by the cloud-agent user.
if [ -n "$BUILD_USER" ] && [ "$(id -un)" = "root" ] && [ "$BUILD_USER" != "root" ]; then
    chown -R "$BUILD_USER:$BUILD_USER" "$REPO_ROOT"
    sudo -u "$BUILD_USER" -H bash -lc "cd \"$REPO_ROOT\" && make clean && make"
else
    cd "$REPO_ROOT"
    make clean
    make
fi
