#!/bin/sh
# Host gcc smoke binary + optional Emscripten MODULARIZE module for the browser lab.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

INC="-Ikernel/drivers/freestanding_x86_64 -Ikernel/freestanding/x86_64"
DEFS="-DFL_WASM_SHELL_PROMPT=1"
# SECFEAT=1 compiles the netlab security lab into the wasm shell (for the
# MitM + RS page); default is lab-free (the Flinstone page).
[ "${SECFEAT:-0}" = "1" ] && DEFS="$DEFS -DFL_SECFEAT"
CFLAGS="-std=gnu11 -Wall -Wextra -Wno-unused-parameter -O2 $DEFS $INC"

# shellcheck disable=SC2086
SRCS="
kernel/freestanding/x86_64/identity.c
kernel/freestanding/x86_64/ramfs.c
kernel/freestanding/x86_64/labdisk.c
kernel/freestanding/x86_64/labnet.c
kernel/freestanding/x86_64/commands.c
kernel/freestanding/x86_64/shell.c
tools/browser-lab/wasm/platform_wasm.c
tools/browser-lab/wasm/wasm_main.c
"

# The security variant's module goes to build/ so the tracked lab wasm stays lab-free.
if [ "${SECFEAT:-0}" = "1" ]; then
  OUT_DIR="$ROOT/build/wasm-secfeat"
else
  OUT_DIR="$ROOT/tools/browser-lab/wasm"
fi
mkdir -p "$OUT_DIR" "$ROOT/dist"

HOST_BIN="$ROOT/dist/flintstone_wasm_host"
# shellcheck disable=SC2086
${CC:-gcc} $CFLAGS -o "$HOST_BIN" $SRCS
echo "host: $HOST_BIN"

if command -v emcc >/dev/null 2>&1; then
  # shellcheck disable=SC2086
  emcc $CFLAGS \
    --no-entry \
    -s MODULARIZE=1 \
    -s EXPORT_NAME=createFlintstoneShell \
    -s INVOKE_RUN=0 \
    -s EXPORTED_FUNCTIONS='["_wasm_shell_init","_wasm_shell_type","_wasm_shell_line","_wasm_vga_buffer","_wasm_vga_bytes"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ENVIRONMENT=web \
    -s FILESYSTEM=0 \
    -s ERROR_ON_UNDEFINED_SYMBOLS=1 \
    -o "$OUT_DIR/flintstone.js" \
    $SRCS
  echo "wasm: $OUT_DIR/flintstone.js"
elif [ "${EMCC_REQUIRED:-0}" = "1" ]; then
  echo "emcc is required (EMCC_REQUIRED=1) but was not found" >&2
  exit 1
else
  echo "emcc not found; skipping WebAssembly module (host binary still built)"
fi
