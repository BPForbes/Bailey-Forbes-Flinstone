#!/usr/bin/env python3
"""
server_bridge.py — TCP relay bridge for Bailey-Forbes-Flinstone (WSL2)

Listens on 0.0.0.0:<port> (all Windows adapters, including Wi-Fi) and
forwards each connection to 127.0.0.1:<port>, which WSL2 routes to the
Flinstone server running in WSL.

No administrator rights required for ports >= 1024.

Usage (run on Windows / in WSL via interop):
  python3 tools/server_bridge.py <port>

LAN peers can then reach the Flinstone server at:
  server join <windows-wifi-ip>:<port>

See also: FlinstonePowershell server-bridge <port>
          (compiled C++ version; same protocol, no Python required)
"""

import asyncio
import sys


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
) -> None:
    peer = client_w.get_extra_info("peername", ("<unknown>", 0))
    try:
        wsl_r, wsl_w = await asyncio.open_connection("127.0.0.1", port)
    except OSError as exc:
        print(
            f"bridge: {peer[0]}:{peer[1]} → WSL failed: {exc}",
            flush=True,
        )
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


async def _main(port: int) -> None:
    server = await asyncio.start_server(
        lambda r, w: _handle(r, w, port),
        "0.0.0.0",
        port,
    )
    bound = ", ".join(str(s.getsockname()) for s in server.sockets)
    print(f"bridge: {bound} → 127.0.0.1:{port}  (Ctrl-C to stop)", flush=True)
    async with server:
        await server.serve_forever()


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: server_bridge.py <port>", file=sys.stderr)
        sys.exit(1)
    try:
        port = int(sys.argv[1])
        if not 1 <= port <= 65535:
            raise ValueError
    except ValueError:
        print(f"server_bridge.py: invalid port '{sys.argv[1]}'", file=sys.stderr)
        sys.exit(1)

    try:
        asyncio.run(_main(port))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
