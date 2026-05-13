#!/usr/bin/env python3
"""
Standalone smoke tests for scripts/mock-agamemnon.py.

Runs without any third-party dependencies (no pytest, no requests). Boots the
mock on an ephemeral port in a background thread and exercises HTTP endpoints
with the stdlib's http.client. Exits non-zero on the first failed assertion.

Run directly:

    python3 scripts/test-mock-agamemnon.py

Regression coverage for issue #89: /v1/chaos/test-empty must reject ALL bodies
(empty and non-empty) so callers cannot accidentally register a 'test-empty'
fault.
"""

from __future__ import annotations

import http.client
import importlib.util
import socket
import sys
import threading
import time
from http.server import HTTPServer
from pathlib import Path


def _load_mock_module():
    """Load scripts/mock-agamemnon.py as a module (it has a hyphen in its name)."""
    mock_path = Path(__file__).resolve().parent / "mock-agamemnon.py"
    spec = importlib.util.spec_from_file_location("mock_agamemnon", mock_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load module from {mock_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def _wait_ready(port: int, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=0.5)
            conn.request("GET", "/v1/health")
            resp = conn.getresponse()
            resp.read()
            conn.close()
            if resp.status == 200:
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"mock did not become ready on :{port}")


def _post(port: int, path: str, body: str, content_type: str = "application/json"):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=2.0)
    conn.request("POST", path, body=body, headers={"Content-Type": content_type})
    resp = conn.getresponse()
    data = resp.read().decode()
    conn.close()
    return resp.status, data


def main() -> int:
    mock = _load_mock_module()
    port = _free_port()
    server = HTTPServer(("127.0.0.1", port), mock.AgamemnonHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        _wait_ready(port)

        # /v1/chaos/test-empty: empty body must return 400.
        status, _ = _post(port, "/v1/chaos/test-empty", "")
        assert status == 400, f"empty body: expected 400, got {status}"

        # /v1/chaos/test-empty: non-empty JSON must ALSO return 400 (issue #89).
        status, _ = _post(port, "/v1/chaos/test-empty", '{"active":true}')
        assert status == 400, f"non-empty JSON: expected 400, got {status}"

        # /v1/chaos/test-empty: non-empty text must return 400.
        status, _ = _post(
            port, "/v1/chaos/test-empty", "garbage", content_type="text/plain"
        )
        assert status == 400, f"non-empty text: expected 400, got {status}"

        # No 'test-empty' fault should have been created.
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=2.0)
        conn.request("GET", "/v1/chaos")
        resp = conn.getresponse()
        body = resp.read().decode()
        conn.close()
        assert "test-empty" not in body, (
            f"test-empty fault leaked into /v1/chaos: {body}"
        )

        # Other chaos endpoints still create fault records on valid JSON.
        status, _ = _post(port, "/v1/chaos/latency", '{"delay_ms":10}')
        assert status == 201, f"latency create: expected 201, got {status}"

        # And still reject empty bodies.
        status, _ = _post(port, "/v1/chaos/latency", "")
        assert status == 400, f"latency empty: expected 400, got {status}"
    finally:
        server.shutdown()
        server.server_close()
    print("OK: mock-agamemnon /v1/chaos/test-empty validation passes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
