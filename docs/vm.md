# VM Documentation

## Overview

The VM harness provides a virtual machine for running guest code. It supports:

- x86 real-mode emulation
- Virtual devices (disk, network, timer)
- SDL2-based windowing (optional)
- WSLg/X11 support

## Building

Enable VM build:

```bash
cmake .. -DBUILD_VM=ON
```

For SDL2 support, ensure SDL2 is installed:

```bash
# Ubuntu/Debian
sudo apt install libsdl2-dev

# Or use the deps scripts
./deps/fetch-sdl2.sh
```

## Usage

```bash
./vm_harness <guest_binary>
```

## Architecture

The VM is split into:

- `vm/host/`: Host-side code (windowing, input)
- `vm/devices/`: Virtual device implementations

See `docs/GUEST_CONTRACT.md` for guest code requirements.
