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
    echo "SDL2 headers linked into deps/install/include/ ($(find /usr/include/SDL2 -maxdepth 1 -name '*.h' | wc -l) files)"
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

            # Convert to Windows path for PATH and PowerShell operations.
            if command -v wslpath >/dev/null 2>&1; then
                WIN_BIN_W=$(wslpath -w "$WIN_BIN" 2>/dev/null)
            else
                WIN_BIN_W="$(echo "$WIN_BIN" | sed 's|/mnt/c|C:|; s|/|\\\\|g')"
            fi

            echo "FlinstonePowershell.exe installed to: $WIN_BIN"

            # Auto-add to Windows user PATH via PowerShell interop so no manual
            # Environment Variables dialog is needed.
            if command -v powershell.exe >/dev/null 2>&1 && [ -n "$WIN_BIN_W" ]; then
                _ps_out=$(powershell.exe -NoProfile -Command "
\$b = '$WIN_BIN_W'
\$p = [Environment]::GetEnvironmentVariable('PATH', 'User')
if ((\$p -split ';') -notcontains \$b) {
    [Environment]::SetEnvironmentVariable('PATH', (\$p + ';' + \$b), 'User')
    Write-Output 'added'
} else {
    Write-Output 'already'
}" 2>/dev/null | tr -d '\r\n')
                if [ "$_ps_out" = "added" ]; then
                    echo "Added $WIN_BIN_W to Windows user PATH (open a new WSL terminal to pick it up)"
                elif [ "$_ps_out" = "already" ]; then
                    echo "$WIN_BIN_W is already on Windows user PATH"
                else
                    echo ""
                    echo "ACTION REQUIRED — add to Windows PATH manually:"
                    echo "  $WIN_BIN_W"
                    echo "  System Properties → Environment Variables → User PATH → New"
                fi
            else
                echo ""
                echo "ACTION REQUIRED — add to Windows PATH manually:"
                echo "  $WIN_BIN_W"
                echo "  System Properties → Environment Variables → User PATH → New"
            fi
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

    # Auto-source fl-wifi.env in the real user's .bashrc so FL_NET_WIFI_FLINSTONE_PS
    # is set in every future WSL session without any manual step.
    if [ -f "$WIFI_ENV" ]; then
        if [ -n "$SUDO_USER" ]; then
            REAL_HOME=$(getent passwd "$SUDO_USER" 2>/dev/null | cut -d: -f6)
        else
            REAL_HOME="$HOME"
        fi
        BASHRC="${REAL_HOME}/.bashrc"
        ENV_LINE="[ -f \"$WIFI_ENV\" ] && . \"$WIFI_ENV\""
        if [ -f "$BASHRC" ] && ! grep -qF "fl-wifi.env" "$BASHRC" 2>/dev/null; then
            printf '\n# Flinstone Wi-Fi / FlinstonePowershell.exe path\n%s\n' "$ENV_LINE" >> "$BASHRC"
            echo "Added fl-wifi.env auto-source to $BASHRC (active in new shells)"
        fi
    fi
    fi

    # Configure WSL2 mirrored networking via .wslconfig — pure Linux file I/O,
    # no cmd.exe / powershell.exe / Windows API calls required.
    # In mirrored mode WSL's eth0 gets the same IP as the Windows Wi-Fi adapter
    # (e.g. 192.168.1.x), so 'server host :<port>' binds directly to the LAN
    # and Pi peers can join without any portproxy or firewall rules.
    _wsl_win_home=""
    # Strategy 1: wslvar (available in many WSL2 distros, no Windows interop).
    if command -v wslvar >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
        _wp=$(wslvar USERPROFILE 2>/dev/null | tr -d '\r\n')
        [ -n "$_wp" ] && _wsl_win_home=$(wslpath "$_wp" 2>/dev/null)
    fi
    # Strategy 2: invoking user's Windows profile under /mnt/c/Users/<name>.
    if [ -z "$_wsl_win_home" ] || [ ! -d "$_wsl_win_home" ]; then
        _invoking=""
        for _u in "${SUDO_USER:-}" "${LOGNAME:-}" "${USER:-}"; do
            if [ -n "$_u" ] && [ "$_u" != "root" ]; then
                _invoking="$_u"
                break
            fi
        done
        if [ -n "$_invoking" ]; then
            _candidate="/mnt/c/Users/$_invoking"
            if [ -d "$_candidate" ]; then
                _wsl_win_home="$_candidate"
            fi
        fi
    fi
    # Strategy 3: glob /mnt/c/Users/ — first non-system directory (no Windows call).
    if [ -z "$_wsl_win_home" ] || [ ! -d "$_wsl_win_home" ]; then
        for _d in /mnt/c/Users/*/; do
            _n=$(basename "$_d")
            case "$_n" in Public|Default*|"All Users") continue ;; esac
            [ -d "$_d" ] && { _wsl_win_home="$_d"; break; }
        done
    fi
    if [ -n "$_wsl_win_home" ] && [ -d "$_wsl_win_home" ]; then
        _wslcfg="${_wsl_win_home}/.wslconfig"
        if ! grep -qi 'networkingMode' "$_wslcfg" 2>/dev/null; then
            printf '\n[wsl2]\nnetworkingMode=mirrored\n' >> "$_wslcfg"
            echo ""
            echo "WSL2 mirrored networking configured in $_wslcfg"
            echo "ACTION REQUIRED: run 'wsl --shutdown' in Windows, then reopen WSL."
            echo "After restart 'server host :<port>' will bind directly to your LAN IP"
            echo "and Pi peers can join without any Windows portproxy or firewall rules."
        else
            echo ".wslconfig already has networkingMode — skipping mirrored networking setup."
        fi
    else
        echo ""
        echo "Could not locate Windows user home for .wslconfig (sudo context?)."
        echo "To enable WSL2 mirrored networking manually, add to %USERPROFILE%\\.wslconfig:"
        echo "  [wsl2]"
        echo "  networkingMode=mirrored"
        echo "Then run 'wsl --shutdown' and reopen WSL."
    fi
fi

echo ""
echo "All dependencies installed."
echo "Optional AArch64 cross-deps (OpenSSL/SQLite static libs):"
echo "  make deps-openssl-aarch64"
echo "  make deps-sqlite-aarch64"
