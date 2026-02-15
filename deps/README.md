# External Dependencies

Flinstone can fetch and build optional libraries locally instead of using system packages.

## Usage

```bash
make deps          # Fetch and build SDL2 into deps/install
make vm-sdl        # Build with local SDL2 (uses deps/install if present)
```


## Behavior

- **`make deps`**: Builds SDL2 (cmake) and CUnit (autotools) into `deps/install`.
- If `deps/install` exists, builds use `-Ideps/install/include` and `-Ldeps/install/lib`.
- SDL2: else uses `pkg-config sdl2`. CUnit: else uses system `-lcunit`.

## Requirements

```bash
apt install build-essential cmake curl automake autoconf libtool
```

- **SDL2**: cmake, C/C++ compilers
- **CUnit**: autoconf, automake, libtool, C compiler

Fallback: `apt install libsdl2-dev libcunit1-dev`
