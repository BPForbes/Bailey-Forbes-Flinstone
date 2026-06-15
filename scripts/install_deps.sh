#!/bin/sh
# Install all build and runtime dependencies for Bailey-Forbes-Flinstone.
# Covers: GCC, GNU assembler (GAS/binutils), AArch64 cross toolchain,
# OpenSSL, SQLite, SDL2, wpa_supplicant/NetworkManager (Linux Wi-Fi),
# and common test/networking tools.
# On WSL: also installs mingw-w64, builds FlinstonePowershell.exe, and
# copies it to the Windows user bin directory.
# Requires: Debian/Ubuntu (apt). Run with sudo or as root.
set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Detect WSL: check /proc/version first, fall back to WSL_DISTRO_NAME env var.
IS_WSL=0
if grep -qiE "microsoft|wsl" /proc/version 2>/dev/null; then
    IS_WSL=1
elif [ -n "${WSL_DISTRO_NAME:-}" ]; then
    IS_WSL=1
fi

apt-get update -qq

apt-get install -y \
    build-essential \
    gcc \
    g++ \
    make \
    binutils \
    nasm \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    pkg-config \
    curl \
    ca-certificates \
    cmake \
    autoconf \
    automake \
    libtool \
    bzip2 \
    tar \
    libssl-dev \
    openssl \
    libsqlite3-dev \
    libcunit1-dev \
    libsdl2-dev \
    iproute2 \
    tcpdump \
    tmux \
    wpasupplicant \
    network-manager

# Wire SDL2 system headers into deps/install/include so `make vm-sdl` finds <SDL.h>.
# libsdl2-dev installs under /usr/include/SDL2/ but the VM build uses -Ideps/install/include.
if [ -d /usr/include/SDL2 ]; then
    mkdir -p "$REPO_ROOT/deps/install/include"
    for f in /usr/include/SDL2/*.h; do
        ln -sf "$f" "$REPO_ROOT/deps/install/include/$(basename "$f")"
    done
    echo "SDL2 headers linked into deps/install/include/ ($(ls /usr/include/SDL2/*.h | wc -l) files)"
fi

if [ "$IS_WSL" = "1" ]; then
    echo ""
    echo "WSL detected — installing mingw-w64 for FlinstonePowershell.exe cross-compile..."
    apt-get install -y mingw-w64

    echo "Building FlinstonePowershell.exe..."
    make -C "$REPO_ROOT" flinstone-ps-windows || {
        echo "Warning: FlinstonePowershell.exe build failed; re-run 'make flinstone-ps-windows' manually."
    }

    FPS_EXE="$REPO_ROOT/tools/FlinstonePowershell/FlinstonePowershell.exe"
    WIFI_ENV="$REPO_ROOT/tools/fl-wifi.env"
    if [ -f "$FPS_EXE" ]; then
        printf 'export FL_NET_WIFI_FLINSTONE_PS="%s"\n' "$FPS_EXE" >"$WIFI_ENV"
        echo "Wrote $WIFI_ENV (source it or add to ~/.bashrc for auto Wi-Fi discovery)"
    fi
    if [ -f "$FPS_EXE" ]; then
        # Resolve the Windows user home directory.
        # Strategy 1: run cmd.exe as the invoking user (not as root) so that
        # %USERPROFILE% reflects the correct Windows home even under sudo.
        WIN_HOME=""
        if command -v cmd.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
            if [ -n "$SUDO_USER" ]; then
                _wp=$(sudo -u "$SUDO_USER" cmd.exe /c "echo %USERPROFILE%" 2>/dev/null | tr -d '\r\n')
            else
                _wp=$(cmd.exe /c "echo %USERPROFILE%" 2>/dev/null | tr -d '\r\n')
            fi
            [ -n "$_wp" ] && WIN_HOME=$(wslpath "$_wp" 2>/dev/null)
        fi
        # Strategy 2: wslvar (available in some WSL2 setups).
        if [ -z "$WIN_HOME" ] || [ ! -d "$WIN_HOME" ]; then
            if command -v wslvar >/dev/null 2>&1; then
                _wp=$(wslvar USERPROFILE 2>/dev/null | tr -d '\r\n')
                [ -n "$_wp" ] && WIN_HOME=$(wslpath "$_wp" 2>/dev/null)
            fi
        fi
        WIN_BIN="${WIN_HOME:+$WIN_HOME/bin}"
        if [ -n "$WIN_HOME" ] && [ -d "$WIN_HOME" ]; then
            mkdir -p "$WIN_BIN"
            cp "$FPS_EXE" "$WIN_BIN/"

            # Convert to Windows path for the PATH instructions.
            if command -v wslpath >/dev/null 2>&1; then
                WIN_BIN_W=$(wslpath -w "$WIN_BIN" 2>/dev/null)
            else
                WIN_BIN_W="$(echo "$WIN_BIN" | sed 's|/mnt/c|C:|; s|/|\\|g')"
            fi

            echo "FlinstonePowershell.exe installed to: $WIN_BIN"
            echo ""
            echo "ACTION REQUIRED — add to Windows PATH (one-time):"
            echo "  $WIN_BIN_W"
            echo "  System Properties → Environment Variables → User PATH → New"
            echo ""
            echo "Then open a new WSL terminal and verify:"
            echo "  which FlinstonePowershell.exe"
        else
            # Home directory could not be determined (common when script runs as
            # root under sudo — cmd.exe loses Windows env context).  Direct the
            # user to the dedicated make target which runs as themselves.
            echo ""
            echo "FlinstonePowershell.exe built successfully."
            echo ""
            echo "Auto-install failed (sudo loses Windows env vars)."
            echo "Run this once as yourself (no sudo) to install it:"
            echo ""
            echo "  make -C '$REPO_ROOT' install-fps-windows"
            echo ""
            echo "Then open a new WSL terminal and verify:"
            echo "  which FlinstonePowershell.exe"
        fi
    fi
fi

echo ""
echo "All dependencies installed."
echo "Optional AArch64 cross-deps (OpenSSL/SQLite static libs):"
echo "  make deps-openssl-aarch64"
echo "  make deps-sqlite-aarch64"
