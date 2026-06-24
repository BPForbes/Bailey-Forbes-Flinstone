#!/usr/bin/env bash
# Unified test driver — runs the standard make test_* matrix in one pass.
# Usage (from repo root):
#   make test_all
#   make test_all-quiet
#   ./scripts/run_all_tests.sh
#
# Optional:
#   FL_TEST_QUICK=1          CI-ish subset (faster smoke)
#   FL_NETNS_PCAP_OK=1       include test_netns_pcap (sudo/netns)
#   FL_TEST_INCLUDE_VM_REPLAY=1   test_replay (runs make clean first — last)
#   FL_TEST_INCLUDE_SHELL_CTRL_C=1   test_shell_ctrl_c (python + shell)
#   FL_TEST_INCLUDE_NETWORK_BRIDGE_PY=1
#   USE_ASM_ALLOC=1          use test_alloc_asm instead of test_alloc_libc
#   SKIP_NETWORK_INTEROP=1   skip check-network-requirements
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

QUIET="${TEST_QUIET:-0}"
MAKE=(make)
if [[ "$QUIET" == "1" ]]; then
  MAKE+=(-s)
fi

passed=0
failed=0
skipped=0
failures=()

run_make() {
  local label="$1"
  shift
  if [[ "$QUIET" == "1" ]]; then
    echo "==> $label"
  else
    echo ""
    echo "================================================================"
    echo "==> $label"
    echo "================================================================"
  fi
  if "${MAKE[@]}" "$@"; then
    passed=$((passed + 1))
    return 0
  fi
  failed=$((failed + 1))
  failures+=("$label")
  return 1
}

run_cmd() {
  local label="$1"
  shift
  if [[ "$QUIET" == "1" ]]; then
    echo "==> $label"
  else
    echo ""
    echo "================================================================"
    echo "==> $label"
    echo "================================================================"
  fi
  if "$@"; then
    passed=$((passed + 1))
    return 0
  fi
  failed=$((failed + 1))
  failures+=("$label")
  return 1
}

skip() {
  echo "==> $1 (skipped: $2)"
  skipped=$((skipped + 1))
}

echo "Flinstone unified test run (TEST_QUIET=${QUIET}, FL_TEST_QUICK=${FL_TEST_QUICK:-0})"

run_make "build shell" BPForbes_Flinstone_Shell

run_make "check-stubs" check-stubs
run_make "test_mem_asm" test_mem_asm
run_make "test_priority_queue" test_priority_queue
run_make "test_workqueue_p18" test_workqueue_p18

if [[ "${USE_ASM_ALLOC:-0}" == "1" ]]; then
  run_make "test_alloc_asm" test_alloc_asm
else
  run_make "test_alloc_libc" test_alloc_libc
fi

run_make "test_drivers" test_drivers
run_make "test_userspace_connection" test_userspace_connection
run_make "test_invariants" test_invariants
run_make "test_issue222" test_issue222
run_make "test_audit_log" test_audit_log
run_make "test_fs_jail" test_fs_jail

run_make "test_server_file_expire" test_server_file_expire
run_make "test_server_file_meta" test_server_file_meta
run_make "test_server_shared_catalog" test_server_shared_catalog
run_make "test_server_shared_landed_name" test_server_shared_landed_name
run_make "test_server_shared_purge" test_server_shared_purge
run_make "test_server_file_accept_path" test_server_file_accept_path
run_make "test_channel_sidecar" test_channel_sidecar
run_make "server_shared_quarantine_harness (build)" server_shared_quarantine_harness
run_cmd "server_shared_quarantine_harness (run)" ./tests/server_shared_quarantine_harness

run_make "test_p0_p2_wiring" test_p0_p2_wiring

if [[ "${FL_TEST_QUICK:-0}" == "1" ]]; then
  skip "P3 server / LAN / UDP / net-tools" "FL_TEST_QUICK=1"
else
  if [[ "${SKIP_NETWORK_INTEROP:-}" == "1" ]]; then
    skip "check-network-requirements" "SKIP_NETWORK_INTEROP=1"
  else
    run_make "check-network-requirements" check-network-requirements
  fi
  run_make "test_p3_server" test_p3_server
  run_make "test_p3_server_lan" test_p3_server_lan
  run_make "test_p3_udp_cmds" test_p3_udp_cmds
  run_make "test_p3_net_tools" test_p3_net_tools
fi

if [[ "$QUIET" == "1" ]]; then
  run_make "test_wifi (quiet bundle)" TEST_QUIET=1 test_wifi
else
  run_make "test_wifi" test_wifi
fi

if [[ "${FL_TEST_QUICK:-0}" == "1" ]]; then
  skip "test_wifi_db / helpers" "FL_TEST_QUICK=1"
else
  run_make "test_wifi_db" test_wifi_db
  run_make "test_wifi_flinstone_helper" test_wifi_flinstone_helper
  run_make "test_wifi_flinstone_linux_helper" test_wifi_flinstone_linux_helper
fi

run_make "test_vm_mem" test_vm_mem
run_make "test_vm_syscall_bridge" test_vm_syscall_bridge
run_make "test_vm_arch_readiness" test_vm_arch_readiness
run_make "test_vm_layer_warning" test_vm_layer_warning

if [[ "${FL_TEST_INCLUDE_SHELL_CTRL_C:-0}" == "1" ]]; then
  run_make "test_shell_ctrl_c" test_shell_ctrl_c
else
  skip "test_shell_ctrl_c" "set FL_TEST_INCLUDE_SHELL_CTRL_C=1"
fi

if [[ "${FL_TEST_INCLUDE_NETWORK_BRIDGE_PY:-0}" == "1" ]]; then
  run_make "test_network_bridge_py" test_network_bridge_py
else
  skip "test_network_bridge_py" "set FL_TEST_INCLUDE_NETWORK_BRIDGE_PY=1"
fi

if [[ "${FL_NETNS_PCAP_OK:-0}" == "1" ]]; then
  run_make "test_netns_pcap" FL_NETNS_PCAP_OK=1 test_netns_pcap
else
  skip "test_netns_pcap" "set FL_NETNS_PCAP_OK=1 (sudo/netns)"
fi

run_cmd "gen_version_changelog (CI)" bash -c \
  'gcc -std=c11 -Wall -Wextra -O2 -o gen_version_changelog scripts/gen_version_changelog.c && ./gen_version_changelog'
run_make "CUnit suite (build)" CHANGELOG_CI=1 BPForbes_Flinstone_Tests
run_cmd "CUnit suite (run)" ./BPForbes_Flinstone_Tests

if [[ "${FL_TEST_INCLUDE_VM_REPLAY:-0}" == "1" ]]; then
  run_make "test_replay (make clean inside)" test_replay
else
  skip "test_replay" "set FL_TEST_INCLUDE_VM_REPLAY=1 (runs make clean)"
fi

echo ""
echo "================================================================"
echo "Unified test summary"
echo "  passed:  $passed"
echo "  failed:  $failed"
echo "  skipped: $skipped"
if [[ "$failed" -gt 0 ]]; then
  echo "Failures:"
  for f in "${failures[@]}"; do
    echo "  - $f"
  done
  echo "================================================================"
  exit 1
fi
echo "All executed tests passed."
echo "================================================================"
exit 0
