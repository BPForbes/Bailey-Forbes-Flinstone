#!/bin/sh
# Install all build and runtime dependencies for Bailey-Forbes-Flinstone.
# Covers: GCC, GNU assembler (GAS/binutils), AArch64 cross toolchain,
# OpenSSL, SQLite, SDL2, wpa_supplicant/NetworkManager (Linux Wi-Fi),
# and common test/networking tools.
# On WSL: also installs mingw-w64 for FlinstonePowershell.exe cross-compile.
# Requires: Debian/Ubuntu (apt). Run with sudo or as root.
set -e

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

if [ "$IS_WSL" = "1" ]; then
    echo ""
    echo "WSL detected — installing mingw-w64 for FlinstonePowershell.exe cross-compile..."
    apt-get install -y mingw-w64
fi

echo ""
echo "All dependencies installed."
echo "Optional AArch64 cross-deps (OpenSSL/SQLite static libs):"
echo "  make deps-openssl-aarch64"
echo "  make deps-sqlite-aarch64"
if [ "$IS_WSL" = "1" ]; then
    echo "FlinstonePowershell.exe (built automatically by \`make\` on WSL):"
    echo "  make flinstone-ps-windows"
    echo "  cp tools/FlinstonePowershell/FlinstonePowershell.exe /mnt/c/Windows/System32/"
fi
