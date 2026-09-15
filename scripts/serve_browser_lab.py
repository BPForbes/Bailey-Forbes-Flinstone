#!/usr/bin/env python3
"""Serve the isolated, local browser lab (no guest execution on this server)."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import atexit
import signal
import subprocess
import sys


def make_handler(directory, coop, coep, frame_ancestors, permissions_policy, corp):
    class Handler(SimpleHTTPRequestHandler):
        def end_headers(self):
            if coop:
                self.send_header("Cross-Origin-Opener-Policy", coop)
            if coep:
                self.send_header("Cross-Origin-Embedder-Policy", coep)
            if corp:
                self.send_header("Cross-Origin-Resource-Policy", corp)
            if frame_ancestors:
                self.send_header("Content-Security-Policy", f"frame-ancestors {frame_ancestors}")
            if permissions_policy:
                self.send_header("Permissions-Policy", permissions_policy)
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

    return partial(Handler, directory=str(directory))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--directory", default="")
    parser.add_argument("--no-coop-coep", action="store_true")
    parser.add_argument("--coep-credentialless", action="store_true",
                        help="Send COEP: credentialless instead of require-corp (portfolio parent)")
    parser.add_argument("--frame-ancestors", default="")
    parser.add_argument("--permissions-policy", default="")
    parser.add_argument("--corp", default="cross-origin")
    parser.add_argument("--relay-port", type=int, default=0,
                        help="Start the JS session relay on this port (browser-hosted online lab only)")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    root = Path(args.directory).resolve() if args.directory else repo
    relay_proc = None
    if args.relay_port:
        candidates = [
            repo / "tools/browser-lab/server-relay-hub.mjs",
            root / "server-relay-hub.mjs",
            root / "tools/browser-lab/server-relay-hub.mjs",
        ]
        relay_script = next((path for path in candidates if path.is_file()), None)
        if relay_script is None:
            print("Missing relay hub (looked in repo tools/browser-lab and the served directory)", file=sys.stderr)
            sys.exit(1)
        relay_proc = subprocess.Popen(
            ["node", str(relay_script), "--bind", args.bind, "--port", str(args.relay_port)],
            cwd=str(relay_script.parent),
        )

        def _stop_relay():
            if relay_proc and relay_proc.poll() is None:
                relay_proc.terminate()
                try:
                    relay_proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    relay_proc.kill()

        atexit.register(_stop_relay)
    coop = None if args.no_coop_coep else "same-origin"
    coep = None if args.no_coop_coep else ("credentialless" if args.coep_credentialless else "require-corp")
    handler = make_handler(root, coop, coep, args.frame_ancestors,
                           args.permissions_policy, args.corp)
    server = ThreadingHTTPServer((args.bind, args.port), handler)

    def _stop(*_):
        if relay_proc and relay_proc.poll() is None:
            relay_proc.terminate()
            try:
                relay_proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                relay_proc.kill()
        server.shutdown()
        sys.exit(0)

    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    print(f"Browser lab: http://{args.bind}:{args.port}/", flush=True)
    server.serve_forever()
