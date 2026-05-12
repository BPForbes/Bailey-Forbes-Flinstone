# Build Documentation

## Prerequisites

- CMake 3.15 or later
- GCC or Clang
- NASM (for x86_64 builds)
- aarch64-linux-gnu toolchain (for cross-compiling to ARM64)

## Native AArch64 Linux (e.g. Raspberry Pi)

Building with **`make vm`** or **`make vm-sdl`** compiles many large sources. On devices with **1–2 GiB RAM**, parallel `gcc` processes can trigger the **OOM killer** (`fatal error: Killed signal terminated program cc1`). The root **Makefile** defaults to a **serial** sub-make on native AArch64 Linux for those targets. If a build is still killed, add **swap** (`dphys-swapfile`, `zram`, or a swap partition) or run `make -j1` at the top level. Override when you have headroom: `make vm VM_SUBMAKE_JOBS=-j4`.

## Quick Start

### x86_64 Build

```bash
./scripts/build_x86_64.sh
```

Or manually:

```bash
mkdir build-x86_64
cd build-x86_64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchains/x86_64.cmake -DTARGET_ARCH=x86_64
cmake --build .
```

### aarch64 Build (Cross-compile)

```bash
./scripts/build_aarch64.sh
```

Or manually:

```bash
export CROSS_COMPILE=aarch64-linux-gnu-
mkdir build-aarch64
cd build-aarch64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchains/aarch64.cmake -DTARGET_ARCH=aarch64
cmake --build .
```

## Build Options

- `BUILD_VM`: Build VM harness (default: OFF)
- `BUILD_TESTS`: Build tests (default: ON)
- `CMAKE_BUILD_TYPE`: Debug, Release, RelWithDebInfo, MinSizeRel

## Architecture Selection

The build system automatically detects the target architecture, but you can override it:

```bash
cmake .. -DTARGET_ARCH=x86_64
cmake .. -DTARGET_ARCH=aarch64
```
