#!/usr/bin/env python3
"""Same-origin /lab-dns used by the browser lab for guest ping."""
import json
import sys
import threading
from http.server import ThreadingHTTPServer
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import serve_browser_lab as lab  # noqa: E402


def test_localhost():
    v4, v6 = lab.lab_dns_resolve("localhost")
    assert v4 == "127.0.0.1", v4
    assert v6 in ("", "::1"), v6


def test_bad_name():
    assert lab.lab_dns_resolve("bad host") == ("", "")
    assert lab.lab_dns_resolve("") == ("", "")


def test_http():
    handler = lab.make_handler(ROOT, "same-origin", "require-corp", "", "", "same-origin")
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        port = server.server_address[1]
        with urlopen(f"http://127.0.0.1:{port}/tools/browser-lab/lab-dns?name=localhost", timeout=3) as resp:
            body = json.loads(resp.read().decode())
        assert body["ok"] is True, body
        assert body["ip"] == "127.0.0.1", body
        with urlopen(f"http://127.0.0.1:{port}/lab-dns?name=localhost", timeout=3) as resp:
            body = json.loads(resp.read().decode())
        assert body["ip"] == "127.0.0.1", body
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    test_localhost()
    test_bad_name()
    test_http()
    print("test_lab_dns: PASS")
