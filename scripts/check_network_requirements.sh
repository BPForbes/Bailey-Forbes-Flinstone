#!/usr/bin/env bash
# P3 network requirements for CI (**contract_p0_ci.h**).
# Targets come from env (no hardcoded hosts in C); defaults below are loopback-only.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "${SKIP_NETWORK_INTEROP:-}" == "1" ]]; then
  echo "SKIP_NETWORK_INTEROP=1: skipping P3 network requirement checks (log rationale per P0-3)."
  exit 0
fi

# Targets are env-only (not compiled into the shell). Default loopback TCP:9 when ICMP is blocked in CI.
P3_PROBE_HOST="${P3_PROBE_HOST:-127.0.0.1}"
P3_PROBE_PORT="${P3_PROBE_PORT:-9}"

echo "[network-requirements] unit tests"
make test_p3_network

echo "[network-requirements] shell: ping + check requirements (${P3_PROBE_HOST}:${P3_PROBE_PORT})"
make -j4
./BPForbes_Flinstone_Shell ping "${P3_PROBE_HOST}" "${P3_PROBE_PORT}"
./BPForbes_Flinstone_Shell check requirements "${P3_PROBE_HOST}" "${P3_PROBE_PORT}"

echo "[network-requirements] OK"
