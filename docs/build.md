# Build Documentation

## Prerequisites

- CMake 3.15 or later
- GCC or Clang
- NASM (for x86_64 builds)
- aarch64-linux-gnu toolchain (for cross-compiling to ARM64)

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
