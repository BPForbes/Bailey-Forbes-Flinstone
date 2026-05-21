# External Dependencies

Flinstone can fetch and build optional libraries locally instead of using system packages.

## Usage

```bash
make deps                  # Fetch and build SDL2 + CUnit into deps/install
make deps-sqlite-aarch64   # AArch64 cross SQLite static lib (deps/install-aarch64)
make deps-openssl-aarch64  # AArch64 cross OpenSSL libcrypto (deps/install-aarch64)
make vm-sdl                # Build with local SDL2 (uses deps/install if present)
make ARCH=arm              # On x86 hosts, runs fetch-sqlite-aarch64.sh when needed
```


## Behavior

- **`make deps`**: Builds SDL2 (cmake) and CUnit (autotools) into `deps/install`.
- **`make deps-sqlite-aarch64`**: Builds SQLite amalgamation into `deps/install-aarch64` for `make ARCH=arm` cross links (no apt `libsqlite3-dev:arm64` required).
- **`make deps-openssl-aarch64`**: Builds static **libcrypto** into `deps/install-aarch64` for `make ARCH=arm` when apt **`libssl-dev:arm64`** is unavailable (GitHub Actions **build-arm** runs `./deps/fetch-openssl-aarch64.sh`). Alternatively install multiarch **`libssl-dev:arm64`**; the Makefile prefers `deps/install-aarch64` headers when present.
- If `deps/install` exists, builds use `-Ideps/install/include` and `-Ldeps/install/lib`.
- SDL2: else uses `pkg-config sdl2`. CUnit: else uses system `-lcunit`.

## Requirements

```bash
apt install build-essential cmake curl automake autoconf libtool
```

- **SDL2**: cmake, C/C++ compilers
- **CUnit**: autoconf, automake, libtool, C compiler

Fallback: `apt install libsdl2-dev libcunit1-dev`
