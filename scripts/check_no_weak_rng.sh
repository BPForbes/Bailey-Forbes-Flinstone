#!/bin/bash
# Fail if a weak random source returns in the sites that use fl_random_bytes
# (kernel/include/fl/random.h is the one place allowed to read /dev/urandom).
# The rand() test-filler files (disk.c, fat32_host.c, sh.c) are deliberately not listed.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FILES="userland/identity/password_hash.cpp
kernel/core/identity/bridge.c
kernel/core/vfs/kq_checkpoint.c
kernel/core/vfs/server_shared_digest.c
kernel/core/net/net_dns.c
kernel/core/net/net_macvlan.c
kernel/core/net/net_wifi_crypto.c"

FAIL=0
for f in $FILES; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f is missing" >&2
        FAIL=1
        continue
    fi
    if grep -nE '(^|[^A-Za-z0-9_])(srand|rand|random|srandom|rand_r|drand48|lrand48|mrand48|arc4random[a-z_]*|getrandom|getentropy|RAND_bytes|RAND_priv_bytes|RAND_pseudo_bytes)[[:space:]]*\(|xorshift|/dev/u?random' "$f"; then
        echo "ERROR: $f uses a weak or direct random source; use fl_random_bytes (fl/random.h)" >&2
        FAIL=1
    fi
done
if [ "$FAIL" -eq 0 ]; then
    echo "check_no_weak_rng: OK (no weak or direct random source in the converted files)"
fi
exit $FAIL
