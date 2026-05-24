#!/bin/sh
# Issue #220.7: useradd/userdel/passwd batch helpers must be wired in cmd_batch_dispatch.c.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISPATCH="$ROOT/userland/command/cmd_batch_dispatch.c"
fail=0
for pair in \
    "FL_SCMD_USERADD:cmd_useradd_batch_tokens_count" \
    "FL_SCMD_USERDEL:cmd_userdel_batch_tokens_count" \
    "FL_SCMD_PASSWD:cmd_passwd_batch_tokens_count"
do
    case_tag="${pair%%:*}"
    func="${pair#*:}"
    if ! grep -q "$case_tag" "$DISPATCH"; then
        echo "check_batch_dispatch_account_cmds: missing $case_tag in cmd_batch_dispatch.c" >&2
        fail=1
    fi
    if ! grep -q "$func" "$DISPATCH"; then
        echo "check_batch_dispatch_account_cmds: missing $func in cmd_batch_dispatch.c" >&2
        fail=1
    fi
done
if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "check_batch_dispatch_account_cmds: ok"
