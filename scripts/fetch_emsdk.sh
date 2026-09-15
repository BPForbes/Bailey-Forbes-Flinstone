#!/bin/sh
# Clone/activate a pinned Emscripten SDK and print the env script path.
set -eu
VER="${EMSDK_VERSION:-3.1.74}"
ROOT="${EMSDK_ROOT:-${HOME}/emsdk}"
if [ ! -x "$ROOT/emsdk" ]; then
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$ROOT"
fi
"$ROOT/emsdk" install "$VER"
"$ROOT/emsdk" activate "$VER"
echo "$ROOT/emsdk_env.sh"
