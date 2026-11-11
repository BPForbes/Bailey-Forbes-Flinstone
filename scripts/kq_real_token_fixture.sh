#!/usr/bin/env bash
# Real-token fixture (Flinstone #407; KeyQuorum KQ-17 #165, KQ-19 #171, KQ-11 #167).
#
# Makes real KeyQuorum material with the keyquorum CLI built from the submodule, in a temporary
# directory, then runs the C test that consumes it:
#   - two devices (keyquorum-device init) and one slot token (KQST) on each, for
#     Root.Flinstone.Bob and Root.Alternative.Ann, each sealed under a passphrase drawn from
#     /dev/urandom at run time (never a literal; kept only in owner-only files in the temp dir);
#   - a third device whose id is a real device id that holds neither token (the wrong-device case);
#   - a KeyQuorum private bridge between Bob and Ann (bridge private create, with Bob's sealed copy
#     of the bridge secret kept in the KeyQuorum store), whose facts the C test copies into
#     Flinstone's account store.
#
# Usage: kq_real_token_fixture.sh TEST_BINARY
# KQ_CLI_DIR names the directory holding keyquorum and keyquorum-device (default: the submodule's
# target/release). Building them is `cargo build --release --locked` in third_party/keyquorum and
# is not part of the default make. Without them, or without python3, the fixture is skipped (exit
# 0) and says so.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_bin="${1:?usage: kq_real_token_fixture.sh TEST_BINARY}"
test_bin="$(cd "$(dirname "${test_bin}")" && pwd)/$(basename "${test_bin}")"
cli_dir="${KQ_CLI_DIR:-${root}/third_party/keyquorum/target/release}"
kq="${cli_dir}/keyquorum"
kqd="${cli_dir}/keyquorum-device"
answer="${root}/scripts/kq_pty_answer.py"

if [ ! -x "${kq}" ] || [ ! -x "${kqd}" ]; then
    echo "test_kq_real_token: SKIPPED: no keyquorum CLI at ${cli_dir}" \
         "(build it with: cargo build --release --locked --manifest-path third_party/keyquorum/Cargo.toml," \
         "or set KQ_CLI_DIR)"
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "test_kq_real_token: SKIPPED: python3 is needed to answer the CLI's terminal prompts"
    exit 0
fi

umask 077
tmp="$(mktemp -d "${TMPDIR:-/tmp}/fl_kq_real_token.XXXXXX")"
trap 'rm -rf -- "${tmp}"' EXIT

# Run-time passphrases (base64 of 24 random bytes), one per account.
head -c 24 /dev/urandom | base64 > "${tmp}/bob.pass"
head -c 24 /dev/urandom | base64 > "${tmp}/ann.pass"

device_id() { sed -n 's/^Initialized .* device \([0-9a-f]\{32\}\)$/\1/p'; }
slot_key() { awk -v k="$1" '$1 == k { print $2 }'; }

bob_dev="$("${kqd}" init "${tmp}/usb-bob" | device_id)"
ann_dev="$("${kqd}" init "${tmp}/usb-ann" | device_id)"
other_dev="$("${kqd}" init "${tmp}/usb-other" | device_id)"
python3 -I "${answer}" "${tmp}/bob.pass" "${kqd}" provision "${tmp}/usb-bob" --label Root.Flinstone.Bob \
    > "${tmp}/bob.slot"
python3 -I "${answer}" "${tmp}/ann.pass" "${kqd}" provision "${tmp}/usb-ann" --label Root.Alternative.Ann \
    > "${tmp}/ann.slot"
bob_enc="$(slot_key encryption < "${tmp}/bob.slot")"
bob_sig="$(slot_key signing < "${tmp}/bob.slot")"
ann_enc="$(slot_key encryption < "${tmp}/ann.slot")"
ann_sig="$(slot_key signing < "${tmp}/ann.slot")"
for v in bob_dev ann_dev other_dev bob_enc bob_sig ann_enc ann_sig; do
    [ -n "${!v}" ] || { echo "test_kq_real_token: the CLI did not print ${v}" >&2; exit 1; }
done

# A private bridge between the two slots, made by KeyQuorum's own tooling. The split tree only
# gives the bridge a key id; its leaves are the members' parents (the implied supervisors).
org="${tmp}/org.sqlite"
for s in "usb-bob Root.Flinstone.Bob" "usb-ann Root.Alternative.Ann"; do
    set -- ${s}
    "${kq}" --db "${org}" device register "${tmp}/$1" --slot "$2" --type encryption > /dev/null
    "${kq}" --db "${org}" device register "${tmp}/$1" --slot "$2" --type signing > /dev/null
done
printf '%s\n' "${bob_enc}" > "${tmp}/bob.enc"
printf '%s\n' "${ann_enc}" > "${tmp}/ann.enc"
# split prints the tree's fresh secret on stdout: it is discarded, the fixture needs only the key id.
"${kq}" --db "${org}" split --label Root --leaf "Root.Flinstone=${tmp}/bob.enc" \
    --leaf "Root.Alternative=${tmp}/ann.enc" > /dev/null
bridge_uid="$("${kq}" --db "${org}" bridge private create 1 \
    --member "Root.Flinstone.Bob=${tmp}/bob.enc" --member "Root.Alternative.Ann=${tmp}/ann.enc" \
    --supervisor "Root.Flinstone=${tmp}/bob.enc" --supervisor "Root.Alternative=${tmp}/ann.enc" \
    --self Root.Flinstone.Bob --output-dir "${tmp}/packages" \
    | sed -n 's/^Created private bridge \([0-9a-f]*\) .*/\1/p')"
[ -n "${bridge_uid}" ] || { echo "test_kq_real_token: bridge private create printed no uid" >&2; exit 1; }

export FL_RT_DIR="${tmp}" FL_RT_ORG_DB="${org}" FL_RT_BRIDGE_UID="${bridge_uid}"
export FL_RT_BOB_DEVICE="${bob_dev}" FL_RT_ANN_DEVICE="${ann_dev}" FL_RT_OTHER_DEVICE="${other_dev}"
export FL_RT_BOB_ENC="${bob_enc}" FL_RT_BOB_SIG="${bob_sig}" FL_RT_ANN_ENC="${ann_enc}" FL_RT_ANN_SIG="${ann_sig}"
export FL_RT_BOB_KQST="${tmp}/usb-bob/vault/slot-Root.Flinstone.Bob/token.kqst"
export FL_RT_ANN_KQST="${tmp}/usb-ann/vault/slot-Root.Alternative.Ann/token.kqst"
cd "${tmp}"
"${test_bin}"
