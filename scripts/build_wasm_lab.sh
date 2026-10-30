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

# KeyQuorum co-signed delivery (tools/browser-lab/wasm/lab_kq.c) links the native checkpoint,
# host and driver code with the KeyQuorum archive built for Emscripten. Only the WebAssembly
# module has it: the host smoke binary above and the QEMU image cannot link Rust.
# KQ_LAB=0 builds the module without it.
KQ_SRCS=""
KQ_FLAGS=""
KQ_LIBS=""
kq_prepare() {
  KQ_CRATE="$ROOT/third_party/keyquorum/ffi/identity"
  KQ_TARGET="${KQ_WASM_TARGET_DIR:-$ROOT/build/kq-wasm-target}"
  KQ_LIB="$KQ_TARGET/wasm32-unknown-emscripten/release/libkeyquorum_identity.a"
  [ -f "$KQ_CRATE/Cargo.toml" ] || { echo "KeyQuorum submodule missing (git submodule update --init)" >&2; return 1; }
  command -v cargo >/dev/null 2>&1 || { echo "cargo is required for KeyQuorum in the lab" >&2; return 1; }
  ( cd "$KQ_CRATE" && CC_wasm32_unknown_emscripten=emcc AR_wasm32_unknown_emscripten=emar \
      CARGO_TARGET_DIR="$KQ_TARGET" cargo build --release --locked --target wasm32-unknown-emscripten ) >&2 || return 1
  # SQLite comes from the archive (rusqlite, bundled); its header is the same crate's.
  sqlite_ver=$(awk '/^name = "libsqlite3-sys"/{getline; gsub(/[^0-9.]/,""); print}' "$KQ_CRATE/Cargo.lock")
  KQ_SQLITE_INC=$(ls -d "${CARGO_HOME:-$HOME/.cargo}"/registry/src/*/"libsqlite3-sys-$sqlite_ver"/sqlite3 2>/dev/null | head -n 1)
  [ -n "$KQ_SQLITE_INC" ] || { echo "sqlite3.h for libsqlite3-sys $sqlite_ver not found" >&2; return 1; }
  KQ_SRCS="
kernel/drivers/keyquorum/kq_driver.c
kernel/drivers/driver_model.c
kernel/core/vfs/kq_checkpoint.c
kernel/core/vfs/kq_delivery.c
kernel/core/vfs/server_catalog_file.c
kernel/core/vfs/server_shared_fs.c
kernel/core/net/net_kq_delivery.c
tools/browser-lab/wasm/lab_kq.c
"
  KQ_FLAGS="-DFL_LAB_KQ -Itools/browser-lab/wasm -I. -Ikernel -Ikernel/include -Ikernel/drivers \
-Ikernel/core/vfs -Ikernel/core/net -Ikernel/core/mm -Ikernel/core/time -Ikernel/core/identity \
-Iuserland/identity -Icontracts/foundations -Icontracts/runtime -Icontracts/identity \
-Icontracts/networking -Icontracts/drivers -Icontracts/storage -Icontracts/hardening \
-Icontracts/observability -Icontracts/operations -Icontracts/virtualization -Ikernel/core/memory \
-Ikernel/core/sched -Ikernel/core/sys -Ikernel/core/platform -Iuserland/shell -Iuserland/command \
-Ithird_party/keyquorum/ffi/identity/include -I$KQ_SQLITE_INC"
  KQ_LIBS="$KQ_LIB"
  KQ_EXPORTS=',"_wasm_kq_roster","_wasm_kq_frame","_wasm_kq_learn","_wasm_kq_public","_wasm_kq_tick","_malloc","_free"'
}

if command -v emcc >/dev/null 2>&1; then
  KQ_EXPORTS=""
  if [ "${KQ_LAB:-1}" = "1" ]; then
    if ! kq_prepare; then
      echo "KeyQuorum for the lab could not be built" >&2
      [ "${EMCC_REQUIRED:-0}" = "1" ] && exit 1
      KQ_SRCS=""; KQ_FLAGS=""; KQ_LIBS=""; KQ_EXPORTS=""
    fi
  fi
  # shellcheck disable=SC2086
  emcc $CFLAGS $KQ_FLAGS \
    --no-entry \
    -s MODULARIZE=1 \
    -s EXPORT_NAME=createFlintstoneShell \
    -s INVOKE_RUN=0 \
    -s EXPORTED_FUNCTIONS="[\"_wasm_shell_init\",\"_wasm_shell_type\",\"_wasm_shell_line\",\"_wasm_vga_buffer\",\"_wasm_vga_bytes\"$KQ_EXPORTS]" \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","UTF8ToString","stringToNewUTF8"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ENVIRONMENT=web,node \
    -s FILESYSTEM=$([ -n "$KQ_SRCS" ] && echo 1 || echo 0) \
    -s ERROR_ON_UNDEFINED_SYMBOLS=1 \
    -o "$OUT_DIR/flintstone.js" \
    $SRCS $KQ_SRCS $KQ_LIBS
  echo "wasm: $OUT_DIR/flintstone.js"
elif [ "${EMCC_REQUIRED:-0}" = "1" ]; then
  echo "emcc is required (EMCC_REQUIRED=1) but was not found" >&2
  exit 1
else
  echo "emcc not found; skipping WebAssembly module (host binary still built)"
fi
