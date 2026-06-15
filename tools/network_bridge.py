#!/usr/bin/env python3
"""
network_bridge.py — WSL2 LAN bridge + FlinstonePowershell helper for Bailey-Forbes-Flinstone

TCP relay (runs on Windows via python.exe / WSL interop):
  python3 tools/network_bridge.py <port>
  python3 tools/network_bridge.py <network> <port>
  Relays <network>:<port> → 127.0.0.1:<port> so LAN peers reach the WSL server.

FlinstonePowershell helpers (run from WSL):
  python3 tools/network_bridge.py discover
  python3 tools/network_bridge.py wifi-scan | wifi-join | wifi-status | wifi-leave
  python3 tools/network_bridge.py bridge-status [--port <n>]
  python3 tools/network_bridge.py env-export

See also: FlinstonePowershell.exe server-bridge [<network>] <port>
"""

from __future__ import annotations

import argparse
import asyncio
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

FPS_REL = Path("tools/FlinstonePowershell/FlinstonePowershell.exe")
SUBCOMMANDS = frozenset({
    "discover",
    "env-export",
    "wifi-scan",
    "wifi-join",
    "wifi-status",
    "wifi-leave",
    "server-proxy",
    "bridge-status",
})


# ---------------------------------------------------------------------------
# FlinstonePowershell discovery / proxy
# ---------------------------------------------------------------------------

def repo_root() -> Path:
    env = os.environ.get("FL_ROOT")
    if env:
        return Path(env).resolve()
    return Path(__file__).resolve().parent.parent


def discover_fps() -> Optional[Path]:
    """Return path to FlinstonePowershell.exe (same search order as the C backend)."""
    env = os.environ.get("FL_NET_WIFI_FLINSTONE_PS")
    if env:
        p = Path(env).expanduser()
        if p.is_file():
            return p.resolve()

    try:
        out = subprocess.check_output(
            ["bash", "-lc", "command -v FlinstonePowershell.exe 2>/dev/null || true"],
            text=True,
        ).strip()
        if out and Path(out).is_file():
            return Path(out).resolve()
    except (subprocess.CalledProcessError, OSError):
        pass

    root = repo_root()
    candidates: List[Path] = [
        root / FPS_REL,
        Path.cwd() / FPS_REL,
    ]
    try:
        shell = Path("/proc/self/exe").resolve()
        candidates.append(shell.parent / FPS_REL)
    except OSError:
        pass

    for c in candidates:
        if c.is_file():
            return c.resolve()
    return None


def run_fps(fps: Path, *args: str) -> Tuple[int, str, str]:
    cmd = [str(fps), *args]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
    except OSError as exc:
        return 127, "", f"{exc}\n"
    return proc.returncode, proc.stdout, proc.stderr


def parse_kv_lines(text: str) -> List[Dict[str, str]]:
    rows: List[Dict[str, str]] = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        row: Dict[str, str] = {}
        for part in line.split("\t"):
            if "=" in part:
                k, v = part.split("=", 1)
                row[k] = v
        if row:
            rows.append(row)
    return rows


def wsl_bind_ipv4() -> Optional[str]:
    try:
        out = subprocess.check_output(["hostname", "-I"], text=True).strip()
        for ip in out.split():
            if not ip.startswith("127.") and not ip.startswith("169.254."):
                return ip
    except (subprocess.CalledProcessError, OSError):
        pass
    return None


def is_wsl() -> bool:
    if os.environ.get("WSL_DISTRO_NAME"):
        return True
    try:
        data = Path("/proc/version").read_text(encoding="utf-8", errors="replace")
        return bool(re.search(r"microsoft|wsl", data, re.I))
    except OSError:
        return False


def cmd_discover(_: argparse.Namespace) -> int:
    fps = discover_fps()
    if not fps:
        print("FlinstonePowershell.exe: not found", file=sys.stderr)
        print("Build: make flinstone-ps-windows", file=sys.stderr)
        return 1
    print(fps)
    return 0


def cmd_env_export(_: argparse.Namespace) -> int:
    fps = discover_fps()
    if not fps:
        return 1
    print(f'export FL_NET_WIFI_FLINSTONE_PS="{fps}"')
    return 0


def cmd_wifi_scan(_: argparse.Namespace) -> int:
    fps = discover_fps()
    if not fps:
        return 1
    rc, out, err = run_fps(fps, "wifi-scan")
    sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    return rc


def cmd_wifi_join(args: argparse.Namespace) -> int:
    fps = discover_fps()
    if not fps:
        return 1
    cmd_args = ["wifi-join", args.ssid]
    if args.password:
        cmd_args.append(args.password)
    rc, out, err = run_fps(fps, *cmd_args)
    sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    return 0 if "result=ok" in out else (rc or 1)


def cmd_wifi_status(_: argparse.Namespace) -> int:
    fps = discover_fps()
    if not fps:
        return 1
    rc, out, err = run_fps(fps, "wifi-status")
    sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    return rc


def cmd_wifi_leave(_: argparse.Namespace) -> int:
    fps = discover_fps()
    if not fps:
        return 1
    rc, out, err = run_fps(fps, "wifi-leave")
    sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    return rc


def cmd_server_proxy(args: argparse.Namespace) -> int:
    fps = discover_fps()
    if not fps:
        return 1
    rc, out, err = run_fps(fps, "server-proxy", args.wsl_ip, str(args.port))
    sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    return 0 if "result=ok" in out else (rc or 1)


def cmd_bridge_status(args: argparse.Namespace) -> int:
    fps = discover_fps()
    wsl_ip = wsl_bind_ipv4() or ""
    win_ip = ""
    ssid = ""
    state = "disconnected"

    if fps:
        _, out, _ = run_fps(fps, "wifi-status")
        for row in parse_kv_lines(out):
            state = row.get("state", state)
            ssid = row.get("ssid", ssid)
            win_ip = row.get("ipv4", win_ip)

    print(f"platform={'wsl' if is_wsl() else 'linux'}")
    print(f"fps={'found' if fps else 'missing'}\tpath={fps or ''}")
    print(f"wsl_bind_ipv4={wsl_ip}")
    print(f"windows_wifi_ipv4={win_ip}")
    print(f"wifi_state={state}\tssid={ssid}")

    if args.port and win_ip and wsl_ip:
        print(f"lan_server_join={win_ip}:{args.port}")
        print(f"wsl_server_host=0.0.0.0:{args.port}  # or {wsl_ip}:{args.port}")
        print(f"relay: python.exe tools/network_bridge.py {win_ip} {args.port}")
    elif args.port:
        print(f"local_server_host=127.0.0.1:{args.port}")
    return 0


def run_subcommand() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("discover").set_defaults(func=cmd_discover)
    sub.add_parser("env-export").set_defaults(func=cmd_env_export)
    sub.add_parser("wifi-scan").set_defaults(func=cmd_wifi_scan)

    p_join = sub.add_parser("wifi-join")
    p_join.add_argument("ssid")
    p_join.add_argument("password", nargs="?", default="")
    p_join.set_defaults(func=cmd_wifi_join)

    sub.add_parser("wifi-status").set_defaults(func=cmd_wifi_status)
    sub.add_parser("wifi-leave").set_defaults(func=cmd_wifi_leave)

    p_proxy = sub.add_parser("server-proxy")
    p_proxy.add_argument("wsl_ip")
    p_proxy.add_argument("port", type=int)
    p_proxy.set_defaults(func=cmd_server_proxy)

    p_status = sub.add_parser("bridge-status")
    p_status.add_argument("--port", type=int, default=0)
    p_status.set_defaults(func=cmd_bridge_status)

    args = parser.parse_args()
    return int(args.func(args))


# ---------------------------------------------------------------------------
# Windows-side TCP relay (asyncio)
# ---------------------------------------------------------------------------

def _detect_windows_ip() -> str:
    try:
        out = subprocess.check_output(
            ["ipconfig"], text=True, stderr=subprocess.DEVNULL
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
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
) -> None:
    peer = client_w.get_extra_info("peername", ("<unknown>", 0))
    try:
        wsl_r, wsl_w = await asyncio.open_connection("127.0.0.1", port)
    except OSError as exc:
        print(f"bridge: {peer[0]}:{peer[1]} → WSL failed: {exc}", flush=True)
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


async def _serve(bind_ip: str, port: int) -> None:
    server = await asyncio.start_server(
        lambda r, w: _handle(r, w, port),
        bind_ip,
        port,
    )
    addrs = ", ".join(str(s.getsockname()) for s in server.sockets)
    print(f"bridge: {addrs} → 127.0.0.1:{port}  (Ctrl-C to stop)", flush=True)
    async with server:
        await server.serve_forever()


def run_tcp_relay() -> None:
    args = sys.argv[1:]
    if not args:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

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
    else:
        bind_ip = args[0]
        try:
            port = int(args[1])
            if not 1 <= port <= 65535:
                raise ValueError
        except ValueError:
            print(f"network_bridge.py: invalid port '{args[1]}'", file=sys.stderr)
            sys.exit(1)

    try:
        asyncio.run(_serve(bind_ip, port))
    except KeyboardInterrupt:
        pass


def main() -> None:
    if len(sys.argv) > 1 and sys.argv[1] in SUBCOMMANDS:
        raise SystemExit(run_subcommand())
    run_tcp_relay()


if __name__ == "__main__":
    main()
