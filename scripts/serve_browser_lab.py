#!/usr/bin/env python3
"""Serve the isolated, local browser lab (no guest execution on this server)."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse


def make_handler(directory, coop_coep, frame_ancestors, permissions_policy, corp):
    class Handler(SimpleHTTPRequestHandler):
        def end_headers(self):
            if coop_coep:
                self.send_header("Cross-Origin-Opener-Policy", "same-origin")
                self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
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
    parser.add_argument("--frame-ancestors", default="")
    parser.add_argument("--permissions-policy", default="")
    parser.add_argument("--corp", default="cross-origin")
    args = parser.parse_args()
    root = Path(args.directory).resolve() if args.directory else Path(__file__).resolve().parents[1]
    handler = make_handler(root, not args.no_coop_coep, args.frame_ancestors,
                           args.permissions_policy, args.corp)
    server = ThreadingHTTPServer((args.bind, args.port), handler)
    print(f"Browser lab: http://{args.bind}:{args.port}/", flush=True)
    server.serve_forever()
