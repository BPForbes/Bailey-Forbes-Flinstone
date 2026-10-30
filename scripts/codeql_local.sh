#!/usr/bin/env bash
# Run CodeQL on this working tree the way .github/workflows/codeql.yml does, and print the
# findings in the review rules' five-part form with their SOC 2 criterion
# (.github/scripts/codeql_report.py). The Security seat runs this before its SOC 2 review; CI
# (`codeql gate`) is the record on the pushed head.
#
#   scripts/codeql_local.sh [--lang LANG]... [--out DIR] [--dry-run]
#
# LANG is c-cpp, javascript-typescript, python or actions (default: all four). CodeQL has no
# assembly extractor: the GAS and NASM layers are not analysed, and say so in the output.
#
# Needs the CodeQL CLI (`codeql` on PATH, or CODEQL=/path/to/codeql) and, for c-cpp, the build
# dependencies of `make` (gcc, g++, libsqlite3-dev, libssl-dev, cargo for KeyQuorum's library).
# Databases and SARIF go to --out (default: a directory under $TMPDIR, outside the repository);
# they are never committed.
#
# Exit status: 0 no finding fails the check; 1 a high or critical finding in shipped code;
# 2 NOT RUN (no CodeQL CLI, a bad argument, or a language that could not be analysed). A seat
# records 2 as "NOT RUN" in its thread, never as a pass.
set -euo pipefail

ALL_LANGS=(c-cpp javascript-typescript python actions)
langs=()
out=""
dry=0

while [ $# -gt 0 ]; do
    case "$1" in
        --lang) [ $# -ge 2 ] || { echo "codeql_local: --lang needs a value" >&2; exit 2; }
                langs+=("$2"); shift 2 ;;
        --out) [ $# -ge 2 ] || { echo "codeql_local: --out needs a value" >&2; exit 2; }
               out="$2"; shift 2 ;;
        --dry-run) dry=1; shift ;;
        -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
        *) echo "codeql_local: unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ ${#langs[@]} -gt 0 ] || langs=("${ALL_LANGS[@]}")

# Query pack and suite per language (the same security-and-quality suite as the config file).
pack_of() {
    case "$1" in
        c-cpp) echo "codeql/cpp-queries:codeql-suites/cpp-security-and-quality.qls" ;;
        javascript-typescript) echo "codeql/javascript-queries:codeql-suites/javascript-security-and-quality.qls" ;;
        python) echo "codeql/python-queries:codeql-suites/python-security-and-quality.qls" ;;
        actions) echo "codeql/actions-queries:codeql-suites/actions-security-and-quality.qls" ;;
        *) return 1 ;;
    esac
}
for l in "${langs[@]}"; do
    pack_of "$l" >/dev/null || { echo "codeql_local: unknown language: $l" >&2; exit 2; }
done

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
codeql_bin="${CODEQL:-codeql}"
out="${out:-${TMPDIR:-/tmp}/codeql-flinstone}"

run() {
    if [ "$dry" -eq 1 ]; then
        printf '+'; printf ' %q' "$@"; printf '\n'
    else
        "$@"
    fi
}

if [ "$dry" -eq 0 ] && ! command -v "$codeql_bin" >/dev/null 2>&1; then
    echo "codeql_local: NOT RUN: the CodeQL CLI is not installed (set CODEQL=/path/to/codeql)." >&2
    echo "codeql_local: the 'codeql gate' check on the pushed head is the record instead." >&2
    exit 2
fi

echo "codeql_local: assembly (GAS .s/.S, NASM .asm) is not analysed: CodeQL has no extractor for it."
mkdir -p "$out"
sarifs=()
failed_langs=()
for lang in "${langs[@]}"; do
    db="$out/db-$lang"
    sarif="$out/$lang.sarif"
    create=("$codeql_bin" database create "$db" --language="$lang" --source-root="$root"
            --codescanning-config=.github/codeql/codeql-config.yml --overwrite)
    if [ "$lang" = "c-cpp" ]; then
        # The KeyQuorum library is built before tracing so only this project's C is extracted;
        # -B rebuilds every object so the tracer sees each compiler call.
        run cargo build --release --locked --manifest-path third_party/keyquorum/ffi/identity/Cargo.toml
        create+=(--command="make -B ARCH=x86_64_gas")
    fi
    if run "${create[@]}" &&
       run "$codeql_bin" database analyze "$db" "$(pack_of "$lang")" --format=sarif-latest \
           --output="$sarif" --sarif-category="/language:$lang" --threads=0; then
        sarifs+=("$sarif")
    else
        echo "codeql_local: NOT RUN: $lang could not be analysed" >&2
        failed_langs+=("$lang")
    fi
done

status=0
if [ "$dry" -eq 1 ]; then
    echo "+ python3 .github/scripts/codeql_report.py --repo-root . ${sarifs[*]:-$out/*.sarif}"
elif [ ${#sarifs[@]} -gt 0 ]; then
    python3 .github/scripts/codeql_report.py --repo-root "$root" "${sarifs[@]}" || status=$?
fi
if [ ${#failed_langs[@]} -gt 0 ]; then
    echo "codeql_local: NOT RUN for: ${failed_langs[*]}" >&2
    [ "$status" -eq 1 ] || status=2
fi
exit "$status"
