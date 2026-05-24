#!/usr/bin/env bash
# P3 network requirements for CI (**contract_p0_ci.h** / **FL_CONTRACT_P0_CI_SURFACE_NETWORK_INTEROP**).
# Honors **SKIP_NETWORK_INTEROP=1** (same value as **FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_VALUE**).
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "${SKIP_NETWORK_INTEROP:-}" == "1" ]]; then
  echo "SKIP_NETWORK_INTEROP=1: skipping P3 network requirement checks (log rationale per P0-3)."
  exit 0
fi

echo "[network-requirements] unit: loopback ICMP"
make test_p3_network

echo "[network-requirements] shell: ping + check requirements"
make -j4
./BPForbes_Flinstone_Shell ping 127.0.0.1
./BPForbes_Flinstone_Shell check requirements

echo "[network-requirements] OK"
