# External Dependencies

Flinstone can fetch and build optional libraries locally instead of using system packages.

## Usage

```bash
make deps          # Fetch and build SDL2 into deps/install
make vm-sdl        # Build with local SDL2 (uses deps/install if present)
```

Or build with local deps explicitly:

```bash
make deps
make VM_ENABLE=1 VM_SDL=1 USE_DEPS=1
```

## Behavior

- **`make deps`**: Downloads SDL2 source, builds with CMake, installs to `deps/install`.
- If `deps/install` exists, the build uses it for SDL2 (`-Ideps/install/include`, `-Ldeps/install/lib`).
- Otherwise, the build uses `pkg-config sdl2` (system SDL2).

## Requirements for `make deps`

- `curl`
- `cmake`
- C and C++ compilers: `build-essential` (gcc, g++, libstdc++)

```bash
apt install build-essential cmake curl
```

If `make deps` fails (e.g. missing libstdc++), use system SDL2 instead:
```bash
apt install libsdl2-dev
make vm-sdl
```
