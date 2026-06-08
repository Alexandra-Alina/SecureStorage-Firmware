#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
server_https.py — TLS 1.2 HTTPS server for the SecureStorage bench.

Same routes and metrics.db as server.py.  Separate port (default 8443).

Usage:
    python server/server_https.py          # port 8443
    python server/server_https.py 9443     # custom port

Certificate:  server/certs/server.crt  (RSA-2048, self-signed)
Key:          server/certs/server.key
Generate with:  python server/gen_cert.py
"""

import http.server
import ssl
import os
import sys
import datetime
import time
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from server import Handler, _open_db, DB_LOCK

PORT      = int(sys.argv[1]) if len(sys.argv) > 1 else 8443
CERTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "certs")
CERT_FILE = os.path.join(CERTS_DIR, "server.crt")
KEY_FILE  = os.path.join(CERTS_DIR, "server.key")

if not os.path.exists(CERT_FILE) or not os.path.exists(KEY_FILE):
    print(f"[error] Certificate files not found in {CERTS_DIR}")
    print(f"        Run:  python server/gen_cert.py")
    sys.exit(1)

# Pre-create DB so first request is instant
with DB_LOCK:
    conn = _open_db(); conn.close()


def _now():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


class TLSHandler(Handler):
    """Verbose wrapper around Handler — logs TLS details + per-request timing."""

    # Suppress the default one-liner log from BaseHTTPRequestHandler
    def log_message(self, fmt, *args):
        pass

    def setup(self):
        super().setup()
        self._conn_start = time.perf_counter()
        # Grab TLS info right after the SSL handshake completes
        try:
            cipher_info = self.connection.cipher()   # (name, protocol, bits)
            peer        = self.connection.getpeername()
            self._cipher   = cipher_info[0] if cipher_info else "?"
            self._protocol = cipher_info[1] if cipher_info else "?"
            self._bits     = cipher_info[2] if cipher_info else "?"
            self._client   = f"{peer[0]}:{peer[1]}"
        except Exception:
            self._cipher = self._protocol = self._bits = self._client = "?"

        print(f"\n{'─'*60}", flush=True)
        print(f"  [{_now()}]  NEW TLS CONNECTION", flush=True)
        print(f"  Client   : {self._client}", flush=True)
        print(f"  Protocol : {self._protocol}", flush=True)
        print(f"  Cipher   : {self._cipher}  ({self._bits} bits)", flush=True)
        print(f"{'─'*60}", flush=True)

    def handle(self):
        """Run all requests on this connection, then log close."""
        super().handle()
        elapsed = (time.perf_counter() - self._conn_start) * 1000
        print(f"  [{_now()}]  CONNECTION CLOSED  ({elapsed:.1f} ms total)", flush=True)
        print(f"{'─'*60}\n", flush=True)

    def handle_one_request(self):
        """Wrap each request with method/path + timing."""
        self._req_start = time.perf_counter()
        super().handle_one_request()

    def send_response(self, code, message=None):
        """Intercept the response to log method + path + status + timing."""
        super().send_response(code, message)
        elapsed = (time.perf_counter() - self._req_start) * 1000
        method  = getattr(self, 'command', '?')
        path    = getattr(self, 'path',    '?')
        clen    = self.headers.get('Content-Length', '-') if hasattr(self, 'headers') and self.headers else '-'
        print(f"  [{_now()}]  {method} {path}", flush=True)
        if method == 'POST' and clen != '-':
            print(f"             Content-Length: {clen} B", flush=True)
        print(f"             -> {code}  ({elapsed:.1f} ms)", flush=True)


# TLS 1.2, RSA key exchange, AES-128-GCM or AES-128-CBC
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ssl.TLSVersion.TLSv1_2
ctx.maximum_version = ssl.TLSVersion.TLSv1_2
ctx.load_cert_chain(CERT_FILE, KEY_FILE)
ctx.set_ciphers("AES128-GCM-SHA256:AES128-SHA256")

print("=" * 60)
print("  SecureStorage HTTPS Telemetry Server  (TLS 1.2)")
print(f"  Listening on  0.0.0.0:{PORT}")
print(f"  Certificate   {CERT_FILE}")
print(f"  Database      shared with server.py (metrics.db)")
print(f"  Started       {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
print("=" * 60)
print(f"[server] GET /           -> SECRET_DATA_1234  (S9 connectivity)")
print(f"[server] POST /metrics   -> store telemetry JSON")
print(f"[server] GET  /metrics   -> dump all rows as JSON")
print(f"[server] Waiting for board (192.168.0.100) via TLS -- Ctrl+C to stop.")
print()

srv = http.server.HTTPServer(("", PORT), TLSHandler)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
try:
    srv.serve_forever()
except KeyboardInterrupt:
    print("\n[server_https] Stopped.")
