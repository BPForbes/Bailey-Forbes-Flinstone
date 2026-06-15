#!/usr/bin/env python3
"""
network_bridge.py — LAN↔WSL2 TCP relay for Bailey-Forbes-Flinstone

Relays connections arriving on a host network adapter to <target>:<port>.
The default target is 127.0.0.1, which WSL2 routes to the Flinstone server.
Use an explicit target such as 10.0.2.15 when the server runs inside a VM.

Usage:
  python3 tools/network_bridge.py [<network>] <port> [<target>]

Parameters:
  <network>  Host-side bind IP (e.g., 192.168.1.235).  When omitted the
             script auto-detects the first non-loopback IPv4 via ipconfig.
  <port>     TCP port to bridge.  Required.
  <target>   Server-side target IP.  Defaults to 127.0.0.1.

No administrator rights needed for ports >= 1024.

Examples:
  python3 tools/network_bridge.py 192.168.1.235 7777
  python3 tools/network_bridge.py 192.168.1.235 7777 10.0.2.15

See also: FlinstonePowershell.exe server-bridge [<network>] <port> [<target>]
          (compiled C++ version; same protocol)
"""

import asyncio
import re
import subprocess
import sys


def _detect_windows_ip() -> str:
    """Return first non-loopback, non-APIPA IPv4 from ipconfig output."""
    out = ""
    for cmd in ("ipconfig.exe", "ipconfig"):
        try:
            out = subprocess.check_output(
                [cmd], text=True, stderr=subprocess.DEVNULL
            )
            break
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    if not out:
        return "0.0.0.0"
    for line in out.splitlines():
        m = re.search(r"IPv4.*?:\s*(\d+\.\d+\.\d+\.\d+)", line, re.IGNORECASE)
        if m:
            addr = m.group(1)
            if not addr.startswith("127.") and not addr.startswith("169.254."):
                return addr
    return "0.0.0.0"


async def _relay(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    try:
        while True:
            data = await reader.read(65536)
            if not data:
                break
            writer.write(data)
            await writer.drain()
    except (ConnectionResetError, BrokenPipeError, OSError):
        pass
    finally:
        try:
            writer.close()
        except OSError:
            pass


async def _handle(
    client_r: asyncio.StreamReader,
    client_w: asyncio.StreamWriter,
    port: int,
    target_ip: str,
) -> None:
    peer = client_w.get_extra_info("peername", ("<unknown>", 0))
    try:
        wsl_r, wsl_w = await asyncio.open_connection(target_ip, port)
    except OSError as exc:
        print(f"bridge: {peer[0]}:{peer[1]} → {target_ip}:{port} failed: {exc}", flush=True)
        client_w.close()
        return
    await asyncio.gather(
        _relay(client_r, wsl_w),
        _relay(wsl_r, client_w),
        return_exceptions=True,
    )
    for w in (wsl_w, client_w):
        try:
            w.close()
        except OSError:
            pass


async def _serve(bind_ip: str, port: int, target_ip: str) -> None:
    server = await asyncio.start_server(
        lambda r, w: _handle(r, w, port, target_ip),
        bind_ip,
        port,
    )
    addrs = ", ".join(str(s.getsockname()) for s in server.sockets)
    print(f"bridge: {addrs} → {target_ip}:{port}  (Ctrl-C to stop)", flush=True)
    async with server:
        await server.serve_forever()


def main() -> None:
    args = sys.argv[1:]
    if not args:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    target_ip = "127.0.0.1"

    if len(args) == 1:
        try:
            port = int(args[0])
            if not 1 <= port <= 65535:
                raise ValueError
        except ValueError:
            print(f"network_bridge.py: invalid port '{args[0]}'", file=sys.stderr)
            sys.exit(1)
        bind_ip = _detect_windows_ip()
        print(f"bridge: auto-detected bind IP: {bind_ip}", flush=True)
    elif len(args) in (2, 3):
        bind_ip = args[0]
        try:
            port = int(args[1])
            if not 1 <= port <= 65535:
                raise ValueError
        except ValueError:
            print(f"network_bridge.py: invalid port '{args[1]}'", file=sys.stderr)
            sys.exit(1)
        if len(args) == 3:
            target_ip = args[2]
    else:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    try:
        asyncio.run(_serve(bind_ip, port, target_ip))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
