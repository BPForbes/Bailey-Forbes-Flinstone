#!/bin/sh
# Clone/activate a pinned Emscripten SDK. Installer chatter goes to stderr so
# command substitution can capture only the env-script path.
set -eu
VER="${EMSDK_VERSION:-3.1.74}"
ROOT="${EMSDK_ROOT:-${HOME}/emsdk}"
if [ ! -x "$ROOT/emsdk" ]; then
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$ROOT" >&2
fi
"$ROOT/emsdk" install "$VER" >&2
"$ROOT/emsdk" activate "$VER" >&2
if [ ! -f "$ROOT/emsdk_env.sh" ]; then
  echo "emsdk activate did not create $ROOT/emsdk_env.sh" >&2
  exit 1
fi
echo "$ROOT/emsdk_env.sh"
